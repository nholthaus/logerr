//--------------------------------------------------------------------------------------------------
// 
//	
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
/// @file	StackTraceException.h
/// @brief	Exception that generates a stack trace of where it occurred
//
//--------------------------------------------------------------------------------------------------

#pragma once
#ifndef StackTraceException_h__
#define StackTraceException_h__

//-------------------------
//	INCLUDES
//-------------------------

#include <exception>

#include <StackTrace.h>
#include <QString>

//-------------------------
//	FORWARD DECLARATIONS
//-------------------------

//--------------------------------------------------------------------------------------------------
//	StackTraceException
//--------------------------------------------------------------------------------------------------
// Don't create these raw... use the ERR() macro
class StackTraceException : public std::exception
{
public:

	StackTraceException(QString errorMessage, QString filename, QString function, size_t line, bool fatal = false);
	
	virtual char const* what() const override;

	QString filename() const;
	QString errorMessage() const;
	QString errorDetails() const;
	QString function() const;
	size_t line() const;
	QString trace() const;
	bool fatal() const;

private:

	QString		m_errorMessage;
	QString		m_fileName;
	QString		m_function;
	size_t		m_line;
	QByteArray	m_what;
	StackTrace	m_trace;
	bool		m_fatal;

};

#endif // StackTraceException_h__
