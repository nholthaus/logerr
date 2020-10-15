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

#include <atomic>
#include <condition_variable>
#include <string>
#include <thread>

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

protected:

	concurrent_queue<std::string>	m_logQueue;
	std::thread						m_thread;
	std::atomic_bool				m_joinAll{false};
};

#endif // LogFileWriter_h__
