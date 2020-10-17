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
#include <appinfo.h>
#include <date.h>
#include <LogFileWriter.h>
#include <timestampLite.h>

// std
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

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
	bool                    finishedConstructing = false;
	std::mutex              constructorMutex;
	std::condition_variable finishedConstructingCV;

	m_thread = std::thread([&, this]
	                       {
		                       if (logFilePath.empty())
		                       {
			                       auto currentDateTime = date::format("%FT%TZ", date::floor<std::chrono::milliseconds>(std::chrono::system_clock::now()));
			                       currentDateTime.erase(std::remove(currentDateTime.begin(), currentDateTime.end(), ':'), currentDateTime.end());

			                       std::ostringstream ss;
			                       ss << APPINFO::logDir() << APPINFO::name() << "_" << currentDateTime << ".log.txt";

			                       logFilePath = ss.str();
		                       }

		                       // If errors occur before the log file can be opened, we really don't have a choice except
		                       // to just write to `std::cerr`.
		                       bool error = false;

		                       if (!std::filesystem::exists(APPINFO::logDir()) && !std::filesystem::create_directories(APPINFO::logDir()))
		                       {
			                       std::cerr << '[' << TimestampLite() << "] [ERROR]    Failed to `mkpath` to the log directory: "
			                                 << APPINFO::logDir() << std::endl;
			                       error = true;
		                       }

		                       // open an unbuffered log file
		                       std::ofstream logFile;
		                       logFile.rdbuf()->pubsetbuf(0, 0);
		                       logFile.open(logFilePath, std::ios::out | std::ios::app);

		                       if (!logFile.is_open())
		                       {
			                       std::cerr << '[' << TimestampLite() << "] [ERROR]    Failed to open the log file for writing: "
			                                 << logFilePath << std::endl;
			                       error = true;
		                       }

		                       // at this point the thread is in steady-state
		                       std::unique_lock<std::mutex> lock(constructorMutex);
		                       finishedConstructing = true;
		                       finishedConstructingCV.notify_one();
		                       lock.unlock();

		                       // don't try to log to a file we couldn't open...
		                       if (error) return;

		                       // log lines as we receive them into the queue
		                       std::string logEntry;
		                       while (!m_joinAll)
		                       {
			                       while (m_logQueue.try_pop_for(logEntry, 100ms))
			                       {
				                       logFile << logEntry;
				                       logFile.flush();
			                       }
		                       }

		                       // close the log on exit
		                       logFile.close();
	                       });

	// wait until the thread hits steady-state to leave the constructor. This will lead to more
	// intuitive behavior (basically the class will behave the same way as if it wasn't threaded).
	std::unique_lock<std::mutex> lock(constructorMutex);
	finishedConstructingCV.wait(lock, [&finishedConstructing]
	                            { return finishedConstructing; });
}

//--------------------------------------------------------------------------------------------------
//	~LogFileWriter (public ) [virtual ]
//--------------------------------------------------------------------------------------------------
/// @brief Destructor
LogFileWriter::~LogFileWriter()
{
	m_joinAll.store(true);
	if (m_thread.joinable())
		m_thread.join();
}

//--------------------------------------------------------------------------------------------------
//	write (public ) []
//--------------------------------------------------------------------------------------------------
/// @brief Queues a string to be written into the log file
/// @param str String (or line) to write to the log
/// @remarks this function is thread-safe
void LogFileWriter::write(std::string str)
{
	m_logQueue.emplace(std::move(str));
}
