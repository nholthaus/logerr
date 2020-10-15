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
#ifndef LogStream_h_
#define LogStream_h_

//-------------------------
//	INCLUDES
//-------------------------

#include <function_view.h>

// std
#include <mutex>
#include <set>
#include <streambuf>
#include <string>
#include <iosfwd>

//--------------------------------------------------------------------------------------------------
//	LogStream
//--------------------------------------------------------------------------------------------------
class LogStream : public std::basic_streambuf<char>
{
public:
	explicit LogStream(std::ostream& stream);
	~LogStream() override;

	void registerLogFunction(function_view<void(std::string)> function);

protected:
	int_type        overflow(int_type v) override;
	std::streamsize xsputn(const char* p, std::streamsize n) override;
	void            log();

private:
	std::ostream&                              m_stream;
	std::streambuf*                            m_old_buf;
	std::set<function_view<void(std::string)>> m_callbacks;
	static thread_local std::string            m_string;
};

#endif    // LogStream_h_
