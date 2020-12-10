//--------------------------------------------------------------------------------------------------
//
//	Timestamp: unit-based timestamping library
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
// Copyright (c) 2016 Nic Holthaus
//
//--------------------------------------------------------------------------------------------------
//
// ATTRIBUTION:
// Parts of this work have been adapted from:
//
//--------------------------------------------------------------------------------------------------
//
/// @file	timestamp.h
/// @brief	returns a timestamp string
//
//--------------------------------------------------------------------------------------------------

#ifndef LOGERR_TIMESTAMPLITE_H
#define LOGERR_TIMESTAMPLITE_H

//------------------------
//	INCLUDES
//------------------------

#include <chrono>
#include <iosfwd>
#include <ostream>
#include <string>
#include <ctime>

//	----------------------------------------------------------------------------
//	CLASS		TimestampLite
//  ----------------------------------------------------------------------------
///	@brief		Functor class that stores a timestamp.
///	@details	By design, the Timestamp is implicitly convertible to a bunch
///				of different c, c++, and string formats. You can use the timestamp
///				like a function by calling Timestamp(), or you can create a timestamp
///				instance, which allows you to review the timestamp value at a later
///				point in the code.
//  ----------------------------------------------------------------------------
class TimestampLite
{
public:
	TimestampLite();

	operator std::chrono::system_clock::time_point() const;
	operator std::time_t() const;
	operator std::string() const;
	friend std::ostream& operator<<(std::ostream& os, const TimestampLite& timestamp);

private:
	std::chrono::system_clock::time_point m_now;
};

#endif    // LOGERR_TIMESTAMPLITE_H