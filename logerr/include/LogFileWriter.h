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
/// @file	LogFileWriter.h
/// @brief	Writes entries to the log file from a separate thread
//
//--------------------------------------------------------------------------------------------------

#pragma once
#ifndef LogFileWriter_h__
#define LogFileWriter_h__

//-------------------------
//	INCLUDES
//-------------------------

#include <concurrent_queue.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>

//-------------------------
//	FORWARD DECLARATIONS
//-------------------------


//--------------------------------------------------------------------------------------------------
//	LogFileWriter
//--------------------------------------------------------------------------------------------------

class LogFileWriter
{
public:

	explicit LogFileWriter(std::string logFilePath = "");
	virtual ~LogFileWriter();

	void write(std::string str);

	/// @brief   The absolute path of the log file THIS writer opened.
	/// @return  the resolved log-file path (the explicit path passed to the constructor, or the auto-generated
	///          <logDir><repo>_<app>_<UTC>.log.txt). Empty only if the file could not be opened. Thread-safe, and
	///          populated by the time the constructor returns (the ctor blocks on the worker's readiness), so a caller
	///          can read the live log's exact path instead of guessing "newest in the directory" - which is wrong when
	///          several processes share one log directory.
	std::string filePath() const;

private:

	/// @brief      Collapse a repeated stack-trace footer in an entry bound for the on-disk log.
	/// @param[in]  entry  the whole log entry (message plus, for an error, a multi-line trace footer).
	/// @return     the entry unchanged the FIRST time a given trace footer is seen; on a later entry carrying the
	///             identical footer, the entry with its footer replaced by a one-line "(trace repeated ...)" note.
	/// @details    Dedup is a FILE-ONLY concern: the live GUI dock always shows every error's full trace, but the
	///             on-disk log stays lean by tracing a given call stack once and noting subsequent repeats. The footer
	///             is the trailing run of frame lines ("    [n]   0x... | function"); it is hashed and looked up in
	///             m_seenTraceFooters (guarded by m_dedupMutex, since write() may be called from several threads).
	std::string deduplicateTraceFooter(std::string entry);

protected:

	concurrent_queue<std::string>      m_logQueue;
	mutable std::mutex                 m_filePathMutex;      ///< guards m_filePath (set on the worker, read by any thread).
	std::string                        m_filePath;           ///< the resolved log-file path this writer opened.
	std::mutex                         m_dedupMutex;         ///< guards m_seenTraceFooters against concurrent write() calls.
	std::unordered_set<std::uint64_t>  m_seenTraceFooters;   ///< hashes of trace footers already written to disk.
	std::jthread                       m_thread;
};

#endif // LogFileWriter_h__
