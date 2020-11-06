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
/// @file	logerrMacros.h
/// @brief	Macro Definitions for the logerr library
//
//--------------------------------------------------------------------------------------------------

#pragma once
#ifndef logerrMacros_h__
#define logerrMacros_h__

//-------------------------
//	INCLUDES
//-------------------------

#include <atomic>
#include <iostream>
#include <thread>
#include <vector>

#include <appinfo.h>
#include <StackTraceException.h>
#include <timestampLite.h>

//------------------------------
//	GLOBALS
//------------------------------

inline std::exception_ptr       g_exceptionPtr = nullptr;
inline std::thread::id          g_mainThreadID;
inline std::atomic_bool         g_mainThreadIDSet = false;
inline int                      g_argc = 0;
inline std::vector<std::string> g_argv;

//-------------------------
//	MACROS
//-------------------------

// LOG FUNCTIONS
#ifndef LOGERR
#define LOGERR std::cout << '[' << TimestampLite() << "] [" << APPINFO::name() << "] [ERROR]    "
#endif
#ifndef LOGWARNING
#define LOGWARNING std::cout << '[' << TimestampLite() << "] [" << APPINFO::name() << "] [WARNING]  "
#endif
#ifndef LOGDEBUG
#define LOGDEBUG std::cout << '[' << TimestampLite() << "] [" << APPINFO::name() << "] [DEBUG]    "
#endif
#ifndef LOGINFO
#define LOGINFO std::cout << '[' << TimestampLite() << "] [" << APPINFO::name() << "] [INFO]     "
#endif

// enable/disable logs
#ifndef LOGERR_DISABLE
#define LOGERR_DISABLE std::cout.setstate(std::ios::failbit)
#endif

#ifndef LOGERR_ENABLE
#define LOGERR_ENABLE std::cout.clear()
#endif

// filename
#ifndef __FILENAME__
#define __FILENAME__ strrchr("\\" __FILE__, '\\') + 1
#endif

// error
#ifndef ERR
#define ERR(msg) (!g_mainThreadIDSet || std::this_thread::get_id() == g_mainThreadID) ? throw StackTraceException(msg, __FILENAME__, __FUNCTION__, __LINE__) : g_exceptionPtr = std::make_exception_ptr(StackTraceException(msg, __FILENAME__, __FUNCTION__, __LINE__));
#endif

// fatal error
#ifndef FATAL_ERR
#define FATAL_ERR(msg) (!g_mainThreadIDSet || std::this_thread::get_id() == g_mainThreadID) ? throw StackTraceException(msg, __FILENAME__, __FUNCTION__, __LINE__, true) : g_exceptionPtr = std::make_exception_ptr(StackTraceException(msg, __FILENAME__, __FUNCTION__, __LINE__, true));
#endif

// expects
#ifndef EXPECTS
#define EXPECTS(condition) \
	if (!(condition)) { ERR("Pre-condition failed: " #condition); }
#endif

// ensures
#ifndef ENSURES
#define ENSURES(condition) \
	if (!(condition)) { ERR("Post-condition failed: " #condition); }
#endif

// verify
#ifndef VERIFY
#define VERIFY(condition) ((!(condition)) ? qt_assert(#condition, __FILENAME__, __LINE__) : qt_noop())
#endif

// TODO
#define __STR2__(x) #x
#define __STR1__(x) __STR2__(x)
#define __LOC__     __FILE__ "(" __STR1__(__LINE__) "): TODO - "
#ifdef Q_OS_WIN
#define TODO(x) __pragma(message(__LOC__ x))
#else
#define DO_PRAGMA(x) _Pragma(#x)
#define TODO(x)      DO_PRAGMA(message("TODO - " #x))
#endif

#endif    // logerrMacros_h__
