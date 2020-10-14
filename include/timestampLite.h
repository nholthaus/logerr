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

#ifndef timestamp_h__
#define timestamp_h__

//------------------------
//	INCLUDES
//------------------------

#include <chrono>
#include <ctime>
#include <iomanip>
#include <cstdint>
#include <string>
#include <iostream>

//------------------------------
//	FORWARD DECLARATIONS
//------------------------------

class QDateTime;
class QDebug;
class QString;

//	----------------------------------------------------------------------------
//	CLASS		TimestampLite
//  ----------------------------------------------------------------------------
///	@brief		Functor class that stores a timestamp.
///	@details	By design, the Timestamp is implicitly convertible to a bunch
///				of different c, c++, and string formats. You can use the timestamp
///				like a function by calling Timestamp(), or you can create a timestamp
///				instance, which allows you to review the timestamp value at a later
///				point in the code.\n\n
//  ---------------------------------------------------------------------------- 
class TimestampLite
{
public:
	
	TimestampLite();

	TimestampLite& update();

	std::chrono::milliseconds fractionalPart() const;
	std::chrono::seconds seconds_since_epoch() const;

	operator std::time_t() const;
	operator std::chrono::system_clock::time_point() const;	
	operator std::string() const;
	operator std::chrono::milliseconds() const;
	operator QDateTime() const;
	operator QString() const;
	operator std::uint64_t() const;
	
	friend std::ostream& operator<<(std::ostream& os, const TimestampLite& t);

	template<typename T>
	inline T to() const
	{
		return static_cast<T>(*this);
	}

private:
	
	std::chrono::system_clock::time_point m_now;
};

QDebug operator<<(QDebug d, TimestampLite t);

#endif // timestamp_h__

// For Emacs
// Local Variables:
// Mode: C++
// c-basic-offset: 2
// fill-column: 116
// tab-width: 4
// End: