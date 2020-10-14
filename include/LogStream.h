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
//	ATTRIBUTION:
//	https://stackoverflow.com/questions/10405739/const-ref-when-sending-signals-in-qt
//
//--------------------------------------------------------------------------------------------------
//
/// @file	LogStream.h
/// @brief	Stream that captures std::cout and passes it on to file logging and a log model
//
//--------------------------------------------------------------------------------------------------

#pragma once
#ifndef LogStream_h__
#define LogStream_h__

//-------------------------
//	INCLUDES
//-------------------------

#include <mutex>
#include <string>
#include <streambuf>

#include <QObject>
#include <QThreadStorage>

//-------------------------
//	FORWARD DECLARATIONS
//-------------------------

namespace std
{
	ostream;
}

//--------------------------------------------------------------------------------------------------
//	LogStream
//--------------------------------------------------------------------------------------------------
class LogStream : public QObject, public std::basic_streambuf<char>
{
	Q_OBJECT

public:

	LogStream(std::ostream& stream);
	virtual ~LogStream();

signals:

	void logEntryReady(std::string str);

protected:

	virtual int_type overflow(int_type v);
	virtual std::streamsize xsputn(const char* p, std::streamsize n);

private:

	std::ostream&						m_stream;
	std::streambuf*						m_old_buf;
	QThreadStorage<std::string>			m_string;
};


#endif // LogStream_h__
