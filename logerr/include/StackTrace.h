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
///	@file			stackTrace.h
///	@brief			Functor class which generates a stack trace from the point where it was called.
/// @details
//
//--------------------------------------------------------------------------------------------------

#ifndef stackTrace_h_
#define stackTrace_h_

//------------------------------
//	INCLUDES
//------------------------------

#include <string>

//	----------------------------------------------------------------------------
//	CLASS		stackTrace
//  ----------------------------------------------------------------------------
///	@brief
///	@details	This class is based in part on the work of Timo Bingmann located
///				here: https://panthema.net/2008/0901-stacktrace-demangled/
/// @note		If you use StackTrace in conjunction with exceptions, know that
///				you must construct the trace ***at the site of the throw***, NOT
///				in the `catch` statement, because the stack will already be
///				unwound at that point.
//  ----------------------------------------------------------------------------
class StackTrace
{
public:

	/**
	 * @brief		Constructor
	 * @param[in]	ignore	Number of items to ignore in the stack trace. Every time this class is
	 *						composed in another class or inherited, this number should be incremented
	 *						by `1`.
	 */
	explicit StackTrace(unsigned int ignore = 0);

	/**
	 * @brief		Stack trace string
	 * @returns		The results of the stack trace as a QString
	*/
	operator std::string() const;
	operator const char*() const;

private:

	static const size_t MAX_FRAMES = 64;				///< Arbitrary.
	static const size_t FUNCTION_NAME_SIZE = 2048;		///< Arbitrary.

	std::string m_value;
};
#endif // stackTrace_h_