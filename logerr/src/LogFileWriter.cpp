//--------------------------------------------------------------------------------------------------
//
//	LOGERR
//
//--------------------------------------------------------------------------------------------------
//
// The MIT License (MIT)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
//--------------------------------------------------------------------------------------------------
//
// Copyright (c) 2020 Nic Holthaus
//
//--------------------------------------------------------------------------------------------------
//
// ATTRIBUTION:
//
//
//--------------------------------------------------------------------------------------------------
//
/// @file	LogFileWriter.cpp
/// @brief	Writes entries to the log file from a separate thread
//
//--------------------------------------------------------------------------------------------------

//----------------------------
//  INCLUDES
//----------------------------

// logerr
#include <LogFileWriter.h>
#include <appinfo.h>
#include <date.h>
#include <logerrMacros.h>
#include <timestampLite.h>

// std
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <string_view>

//----------------------------
//  USING NAMESPACE
//----------------------------

using namespace std::chrono_literals;

//--------------------------------------------------------------------------------------------------
//	LogFileWriter (public ) []
//--------------------------------------------------------------------------------------------------
/// @brief Constructor
/// @param logFilePath path to the log file for this application
LogFileWriter::LogFileWriter(std::string logFilePath)
{
	auto ready = std::make_shared<std::promise<void>>();
	auto readyFuture = ready->get_future();

	m_thread = std::jthread([this, logFilePath = std::move(logFilePath), ready](std::stop_token stop) mutable noexcept
	                       {
		                       bool readyReported = false;
		                       try
		                       {
		                       if (logFilePath.empty())
		                       {
			                       auto currentDateTime = date::format("%FT%TZ", date::floor<std::chrono::milliseconds>(std::chrono::system_clock::now()));
			                       std::erase(currentDateTime, ':');

			                       std::string name = APPINFO::gitRepo();
			                       if (name != APPINFO::name())
			                       {
				                       name.append("_").append(APPINFO::name());
			                       }

			                       std::ostringstream ss;
			                       ss << APPINFO::logDir() << name << "_" << currentDateTime << ".log.txt";

			                       logFilePath = ss.str();
		                       }

		                       // If errors occur before the log file can be opened, we really don't have a choice except
		                       // to just write to `std::cerr`.
		                       bool error = false;

		                       // if the directory doesn't exist, create it
		                       try
		                       {
			                       if (!std::filesystem::exists(APPINFO::logDir()))
			                       {
				                       std::filesystem::create_directories(APPINFO::logDir());
			                       }
		                       }
		                       catch(const std::filesystem::filesystem_error& e)
		                       {
			                       std::cerr << '[' << TimestampLite() << "] [ERROR]    Failed to `mkpath` to the log directory: "
			                                 << APPINFO::logDir() << ". Details: " << e.what() << '\n';
			                       error = true;
		                       }

		                       // open an unbuffered log file
		                       std::ofstream logFile;
		                       logFile.rdbuf()->pubsetbuf(nullptr, 0);
		                       logFile.open(logFilePath, std::ios::out | std::ios::app);

		                       if (!logFile.is_open())
		                       {
			                       std::cerr << '[' << TimestampLite() << "] [ERROR]    Failed to open the log file for writing: "
			                                 << logFilePath << '\n';
			                       error = true;
		                       }

		                       // Publish the resolved path (guarded) BEFORE reporting readiness, so filePath() is
		                       // populated by the time the constructor - which blocks on this readiness - returns.
		                       {
			                       const std::lock_guard<std::mutex> pathLock(m_filePathMutex);
			                       m_filePath = logFilePath;
		                       }

		                       // At this point the worker is initialized. Report readiness before entering the blocking pop.
		                       ready->set_value();
		                       readyReported = true;

		                       // don't try to log to a file we couldn't open...
		                       if (error)
		                       {
			                       return;
		                       }

		                       // log lines as we receive them into the queue
		                       std::string logEntry;
		                       while (m_logQueue.wait_pop(logEntry, stop))
		                       {
			                       logFile << logEntry;
			                       logFile.flush();
		                       }

		                       // close the log on exit
		                       logFile.close();
		                       }
		                       catch (...)
		                       {
			                       const auto failure = std::current_exception();
			                       if (!readyReported)
			                       {
				                       try
				                       {
					                       ready->set_exception(failure);
				                       }
				                       catch (const std::exception& promiseError)
				                       {
					                       std::cerr << "LogFileWriter could not report its startup failure: "
					                                 << promiseError.what() << '\n';
				                       }
			                       }
			                       else
			                       {
				                       logerr::captureException(failure);
			                       }
			                       try
			                       {
				                       std::rethrow_exception(failure);
			                       }
			                       catch (const std::exception& error)
			                       {
				                       std::cerr << "LogFileWriter worker failed: " << error.what() << '\n';
			                       }
			                       catch (...)
			                       {
				                       std::cerr << "LogFileWriter worker failed with a non-standard exception\n";
			                       }
		                       }
	                       });

	// Preserve the synchronous construction contract and propagate initialization failures on the constructing thread.
	readyFuture.get();
}

//--------------------------------------------------------------------------------------------------
//	~LogFileWriter (public ) [virtual ]
//--------------------------------------------------------------------------------------------------
/// @brief Destructor
LogFileWriter::~LogFileWriter()
= default;

//--------------------------------------------------------------------------------------------------
//	write (public ) []
//--------------------------------------------------------------------------------------------------
/// @brief Queues a string to be written into the log file
/// @param str String (or line) to write to the log
/// @remarks this function is thread-safe
void LogFileWriter::write(std::string str)
{
	// Deduplicate a repeated trace footer for the ON-DISK log only (the GUI dock, a separate sink, still shows every
	// full trace). The first entry carrying a given stack keeps its full footer; a later entry with the identical
	// footer is written with the footer collapsed to a one-line note, so the file does not repeat an identical stack.
	m_logQueue.emplace(deduplicateTraceFooter(std::move(str)));
}

//--------------------------------------------------------------------------------------------------
//	deduplicateTraceFooter (private ) []
//--------------------------------------------------------------------------------------------------
/// @brief Collapse a repeated stack-trace footer in a file-bound log entry (see the header for the contract).
std::string LogFileWriter::deduplicateTraceFooter(std::string entry)
{
	// The trace footer is the trailing run of symbolized frame lines. formatFrames emits each as, e.g.,
	//   "    [0  ]   0x00007ff...: slipway.cpp:1519            | Slipway::continueLaunch"
	// Detect the footer's first line by that leading "[<digits>]" + " 0x" shape (indentation-agnostic), and treat from
	// there to the end of the entry as the footer. Everything before it is the message (kept verbatim).
	const auto isFrameLine = [](std::string_view line) noexcept
	{
		std::size_t i = 0;
		while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
		if (i >= line.size() || line[i] != '[') return false;
		++i;
		const std::size_t digitsStart = i;
		while (i < line.size() && line[i] >= '0' && line[i] <= '9') ++i;
		if (i == digitsStart) return false;    // "[" not followed by a frame index
		while (i < line.size() && line[i] == ' ') ++i;
		if (i >= line.size() || line[i] != ']') return false;
		return line.find("0x") != std::string_view::npos;    // a symbolized frame carries a hex address
	};

	// Find the byte offset of the first frame line (scanning line by line). No frame line -> not a traced error; return
	// the entry untouched.
	std::size_t footerStart = std::string::npos;
	std::size_t lineBegin    = 0;
	while (lineBegin < entry.size())
	{
		std::size_t lineEnd = entry.find('\n', lineBegin);
		if (lineEnd == std::string::npos) lineEnd = entry.size();
		if (isFrameLine(std::string_view(entry).substr(lineBegin, lineEnd - lineBegin)))
		{
			footerStart = lineBegin;
			break;
		}
		lineBegin = lineEnd + 1;
	}
	if (footerStart == std::string::npos)
		return entry;    // no trace footer to dedup

	const std::string_view footer(std::string_view(entry).substr(footerStart));

	// FNV-1a over the footer bytes: a stable, cheap key for "the same trace, symbolized identically."
	std::uint64_t hash = 1469598103934665603ULL;
	for (const char c : footer)
	{
		hash ^= static_cast<std::uint8_t>(c);
		hash *= 1099511628211ULL;
	}

	{
		const std::lock_guard<std::mutex> lock(m_dedupMutex);
		if (m_seenTraceFooters.insert(hash).second)
			return entry;    // first time this footer is seen: write it in full
	}

	// A repeat: keep the message (everything before the footer) and replace the footer with a one-line note so the
	// on-disk log records THAT the error recurred on the same stack without re-printing the whole trace.
	std::string collapsed = entry.substr(0, footerStart);
	if (!collapsed.empty() && collapsed.back() != '\n')
		collapsed += '\n';
	collapsed += "    (trace deduplicated - identical call stack already recorded above)\n";
	return collapsed;
}

//--------------------------------------------------------------------------------------------------
//	filePath (public ) []
//--------------------------------------------------------------------------------------------------
/// @brief Returns the absolute path of the log file this writer opened.
/// @remarks this function is thread-safe
std::string LogFileWriter::filePath() const
{
	const std::lock_guard<std::mutex> pathLock(m_filePathMutex);
	return m_filePath;
}
