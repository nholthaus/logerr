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
#ifndef logerrMacros_h_
#define logerrMacros_h_

//-------------------------
//	INCLUDES
//-------------------------

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include <appinfo.h>
#include <StackTrace.h>
#include <logerrTypes.h>

//------------------------------
//	GLOBALS
//------------------------------

inline std::exception_ptr       g_exceptionPtr = nullptr;
inline std::mutex               g_exceptionMutex;
inline std::thread::id          g_mainThreadID;
inline std::atomic_bool         g_mainThreadIDSet = false;
inline int                      g_argc            = 0;
inline std::vector<std::string> g_argv;

namespace logerr
{
	/// Return the last path component for either native Windows or POSIX source paths.
	constexpr const char* sourceFilename(const char* path) noexcept
	{
		const char* filename = path;
		for (const char* cursor = path; *cursor != '\0'; ++cursor)
		{
			if (*cursor == '/' || *cursor == '\\')
				filename = cursor + 1;
		}
		return filename;
	}

	/// Publish the first background-thread failure for cooperative rethrow on the main thread via LOGERR_RETHROW().
	inline void captureException(const std::exception_ptr& failure) noexcept
	{
		const std::lock_guard<std::mutex> lock(g_exceptionMutex);
		if (!g_exceptionPtr)
		{
			g_exceptionPtr = failure;
		}
	}

	/// Consume the pending background-thread failure, if any.
	inline std::exception_ptr takeException() noexcept
	{
		const std::lock_guard<std::mutex> lock(g_exceptionMutex);
		return std::exchange(g_exceptionPtr, nullptr);
	}

	/// De-dup registry for LOGERR's full-trace footer: each distinct call site (file + line) logs its stack trace ONCE,
	/// then only the one-line message on repeats, so a churny error site (a reconnect blip) records the trace once
	/// without spamming an identical trace every occurrence.
	inline std::mutex                                                  g_tracedSitesMutex;
	inline std::unordered_set<std::uint64_t>                           g_tracedSites;

	/// A stable per-call-site key: the __FILE__ pointer (one interned literal per source file, identical across a file's
	/// call sites) folded with __LINE__. Distinct sites in the same file get distinct keys (line differs); the same site
	/// hit repeatedly gets the same key. Pointer identity is stable within a process, so no string hashing is needed.
	inline std::uint64_t traceSiteKey(const char* fileKey, std::uint32_t line) noexcept
	{
		return (reinterpret_cast<std::uint64_t>(fileKey) << 20) ^ line;
	}

	/// True the FIRST time a given call site logs; false on every repeat. Records the site so the footer traces once.
	inline bool firstTraceForSite(const char* fileKey, std::uint32_t line) noexcept
	{
		const std::lock_guard<std::mutex> lock(g_tracedSitesMutex);
		return g_tracedSites.insert(traceSiteKey(fileKey, line)).second;
	}

	/// Forget every recorded trace site, so the NEXT hit of each LOGERR site traces again. For test isolation and for a
	/// caller that wants a fresh full-trace baseline (e.g. at the start of a new run/session).
	inline void resetTracedSites() noexcept
	{
		const std::lock_guard<std::mutex> lock(g_tracedSitesMutex);
		g_tracedSites.clear();
	}

	/// RAII error-log line: prints the [ts][app][ERROR][file:line func] prefix on construction, forwards << to std::cout,
	/// and on destruction appends the FULL stack trace the FIRST time this call site logs (deduped by firstTraceForSite),
	/// then a newline. So every LOGERR carries the whole trace in the log, without a trace-per-line flood on a repeating
	/// site, and every existing `LOGERR << a << b << ENDL` call site keeps compiling unchanged (the trailing ENDL is a
	/// harmless extra newline before the footer).
	class TracingErrorLine
	{
	public:
		/// @param tag  the subsystem/app tag shown in the [tag] field; defaults to APPINFO::name(). A module-scoped
		///             consumer (a per-subsystem logger) passes its own tag here and inherits the identical traced,
		///             deduped behavior instead of forking the macro.
		explicit TracingErrorLine(const char* file, const char* fileKey, std::uint32_t line, const char* function,
		                          const std::string& tag = APPINFO::name())
		    : m_fileKey(fileKey), m_line(line)
		{
			std::cout << '[' << TimestampLite() << "] [" << tag << "] [ERROR]    [" << file << ':' << line
			          << ' ' << function << "]  ";
		}
		~TracingErrorLine()
		{
			if (firstTraceForSite(m_fileKey, m_line))
				std::cout << '\n' << static_cast<std::string>(::StackTrace(2));
			std::cout << std::endl;
		}
		template<typename T>
		TracingErrorLine& operator<<(const T& value)
		{
			std::cout << value;
			return *this;
		}
		// Stream manipulators (std::endl / std::flush / ENDL) are overloaded functions, not a const T&: forward them
		// explicitly so an existing `LOGERR << ... << std::endl` keeps compiling. The trailing endl is harmless (the
		// destructor's trace footer follows on the next line).
		TracingErrorLine& operator<<(std::ostream& (*manip)(std::ostream&))
		{
			std::cout << manip;
			return *this;
		}
	private:
		const char*   m_fileKey;
		std::uint32_t m_line;
	};
}    // namespace logerr

//-------------------------
//	MACROS
//-------------------------

// filename
#ifndef __FILENAME__
#define __FILENAME__ ::logerr::sourceFilename(__FILE__)
#endif

// Full function signature. Unlike __FUNCTION__, this retains the enclosing scope and useful lambda identity instead of
// reporting only `operator()`. Centralized here so every existing ERR/FATAL_ERR/GOTHERE call improves automatically.
#ifndef LOGERR_FUNCTION
#if defined(_MSC_VER)
#define LOGERR_FUNCTION __FUNCSIG__
#elif defined(__clang__) || defined(__GNUC__)
#define LOGERR_FUNCTION __PRETTY_FUNCTION__
#else
#define LOGERR_FUNCTION __func__
#endif
#endif

// LOG FUNCTIONS
// Errors and warnings carry their source location automatically. Info/debug remain compact because they are expected
// operational events rather than diagnostic paths.
#ifndef LOGERR
// LOGERR is a temporary TracingErrorLine: it prints the [ts][app][ERROR][file:line func] prefix, forwards the streamed
// message, and on end-of-statement appends the FULL stack trace the first time this call site logs (deduped, so a
// repeating site records the trace once, not every time). __FILE__ doubles as the per-site de-dup key (a stable pointer
// per source file) alongside __LINE__. Every existing `LOGERR << a << b << ENDL` compiles unchanged.
#define LOGERR ::logerr::TracingErrorLine(__FILENAME__, __FILE__, static_cast<std::uint32_t>(__LINE__), LOGERR_FUNCTION)
#endif
#ifndef LOGWARNING
#define LOGWARNING                                                                                                       \
	(std::cout << '[' << TimestampLite() << "] [" << APPINFO::name() << "] [WARNING]  [" << __FILENAME__ << ':'         \
	           << __LINE__ << ' ' << LOGERR_FUNCTION << "]  ")
#endif
#ifndef LOGDEBUG
#define LOGDEBUG std::cout << '[' << TimestampLite() << "] [" << APPINFO::name() << "] [DEBUG]    "
#endif
#ifndef LOGINFO
#define LOGINFO std::cout << '[' << TimestampLite() << "] [" << APPINFO::name() << "] [INFO]     "
#endif
#ifndef ENDL
#define ENDL std::endl
#endif

// Capture a full trace at this call site without deliberately throwing or changing the caller's control flow. This is
// opt-in because symbolization is substantially more expensive than LOGERR's source-location prefix.
#ifndef LOGERR_TRACE
#define LOGERR_TRACE(msg) (LOGERR << (msg) << '\n' << static_cast<std::string>(::StackTrace(1)) << ENDL)
#endif

// enable/disable logs
#ifndef LOGERR_DISABLE
#define LOGERR_DISABLE std::cout.setstate(std::ios::failbit)
#endif

#ifndef LOGERR_ENABLE
#define LOGERR_ENABLE std::cout.clear()
#endif

// error
#ifndef ERR
#define ERR(msg) throw logerr::exception(msg, __FILENAME__, LOGERR_FUNCTION, __LINE__);
#endif

// fatal error
#ifndef FATAL_ERR
#define FATAL_ERR(msg) throw logerr::exception(msg, __FILENAME__, LOGERR_FUNCTION, __LINE__, true);
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

/// call this in the programs `main` loop, if it has one
#define LOGERR_RETHROW()                                      \
	{                                                         \
		std::exception_ptr exceptionPtr;                      \
		exceptionPtr = logerr::takeException();               \
                                                              \
		if (exceptionPtr)                                     \
		{                                                     \
			if (!g_mainThreadIDSet)                           \
				std::exit(12);                                \
			if (std::this_thread::get_id() != g_mainThreadID) \
				std::exit(13);                                \
                                                              \
			std::rethrow_exception(exceptionPtr);             \
		}                                                     \
	}

// verify
#ifndef VERIFY
#define VERIFY(condition) ((!(condition)) ? qt_assert(#condition, __FILENAME__, __LINE__) : qt_noop())
#endif

// TODO
#define STR2_(x) #x
#define STR1_(x) STR2_(x)
#define LOC_     __FILE__ "(" STR1_(__LINE__) "): TODO - "
#ifdef Q_OS_WIN
#define TODO(x) __pragma(message(LOC_ x))
#else
#define DO_PRAGMA(x) _Pragma(#x)
#define TODO(x)      DO_PRAGMA(message("TODO - " #x))
#endif

// Debug - gothere
#ifndef GOTHERE
#define GOTHERE LOGDEBUG << "Got to " << LOGERR_FUNCTION << " in " << __FILE__ << ":" << __LINE__ << ENDL;
#endif

#endif    // logerrMacros_h_
