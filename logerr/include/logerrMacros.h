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
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <appinfo.h>
#include <StackTrace.h>
#include <asyncTraceLog.h>
#include <logerrTypes.h>

// Raw return-address capture for the deferred error footer. The capture is cheap (a handful of microseconds) and runs
// on the logging thread; the tens-of-milliseconds symbol resolution is deferred to the async trace-log worker. The
// platform capture primitive is isolated behind this header so its (heavy, on Windows) system header is included once.
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <execinfo.h>
#endif

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

	/// Forget every recorded stack, so the NEXT occurrence of each call stack traces again. Forwards to StackTrace's
	/// deduplication registry. For test isolation and for a caller that wants a fresh full-trace baseline (for example at
	/// the start of a new run or session).
	inline void resetTracedSites() noexcept
	{
		StackTrace::resetDeduplication();
	}

	/// Capture the current thread's raw return addresses (cheap, no symbolization), dropping the innermost frames that
	/// belong to logerr itself so the first displayed frame is the caller of LOGERR. The addresses are symbolized later,
	/// off the logging thread, by the async trace-log worker.
	inline std::vector<void*> captureCallStack(unsigned int skipInnermost) noexcept
	{
		constexpr int      maxFrames = 256;
		std::vector<void*> raw(maxFrames);
#if defined(_WIN32)
		const unsigned short captured = CaptureStackBackTrace(0, static_cast<DWORD>(maxFrames), raw.data(), nullptr);
		raw.resize(captured);
#else
		const int captured = ::backtrace(raw.data(), maxFrames);
		raw.resize(captured < 0 ? 0 : static_cast<std::size_t>(captured));
#endif
		if (skipInnermost < raw.size())
			raw.erase(raw.begin(), raw.begin() + skipInnermost);
		else
			raw.clear();
		return raw;
	}

	/// RAII error-log line: builds the [ts][app][ERROR][file:line func] prefix and buffers the streamed message, then on
	/// destruction captures the raw return addresses (cheap) and hands the whole entry to the async trace-log worker,
	/// which symbolizes off the logging thread and writes the message and its FULL stack-trace footer as one contiguous
	/// unit. Deduplication is by STACK, not by call site: a repeat of the identical stack is written message-only (no
	/// trace footer), but a DIFFERENT call path reaching the same LOGERR line always traces in full, so no distinct stack
	/// is ever hidden. So every LOGERR carries its whole trace the first time each unique stack occurs, without a
	/// trace-per-line flood on a churny site, and every existing `LOGERR << a << b << ENDL` call site keeps compiling
	/// unchanged (the trailing ENDL is a harmless extra newline appended to the buffered message).
	class TracingErrorLine
	{
	public:
		/// @param tag  the subsystem/app tag shown in the [tag] field; defaults to APPINFO::name(). A module-scoped
		///             consumer (a per-subsystem logger) passes its own tag here and inherits the identical traced,
		///             deduplicated behavior instead of forking the macro.
		explicit TracingErrorLine(const char* /*file*/, const char* /*fileKey*/, std::uint32_t /*line*/,
		                          const char* /*function*/, const std::string& tag = APPINFO::name())
		{
			// The line LEADS with the message: [ts] [tag] [ERROR] <message>. The source location (file:line) and the
			// function signature are NOT on this line - they are the trace footer's frame 0, which is exactly this call
			// site. Keeping the fat function signature off the headline means the actual error text is what the reader
			// sees first, not a __FUNCSIG__ shoved ahead of it. A deduplicated repeat (no footer) is just the message,
			// which is self-describing. The file/line/function parameters are retained for API/source-compatibility with
			// every existing LOGERR call site but are intentionally not rendered here.
			m_prefix << '[' << TimestampLite() << "] [" << tag << "] [ERROR]    ";
		}
		~TracingErrorLine()
		{
			// When the caller supplied an EXTERNAL footer (an origin diagnostic relayed from another host - e.g. a remote
			// ship's failure on the buoy), write the message + that footer verbatim; the local stack is meaningless for an
			// error that occurred elsewhere. Otherwise capture the raw return addresses on THIS (logging) thread and defer
			// the expensive symbolization to the worker. Skip the two innermost frames - captureCallStack itself and this
			// destructor - so the FIRST displayed trace frame (#0) is the LOGERR call site, not a logerr-internal frame.
			// Since the location no longer appears on the message line, frame 0 IS the locator; it must be the user's site.
			if (m_hasExternalFooter)
				logerr::enqueueTracedError(m_prefix.str(), m_message.str(), std::move(m_externalFooter));
			else
				logerr::enqueueTracedError(m_prefix.str(), m_message.str(), captureCallStack(2), /*deduplicateByStack*/ true);
		}
		/// @brief	Supply an ALREADY-FORMATTED footer (an origin diagnostic from another host) to render beneath the
		///			message INSTEAD of this thread's captured stack. Returns *this so it chains before the streamed message:
		///			`SHIPLOG_ERR.withExternalTrace(buoyOrigin) << reason << ENDL;`. An empty footer leaves the default
		///			local-capture behavior unchanged.
		TracingErrorLine& withExternalTrace(std::string footer)
		{
			if (!footer.empty())
			{
				m_externalFooter    = std::move(footer);
				m_hasExternalFooter = true;
			}
			return *this;
		}
		template<typename T>
		TracingErrorLine& operator<<(const T& value)
		{
			m_message << value;
			return *this;
		}
		// Stream manipulators (std::endl / std::flush / ENDL) are overloaded functions, not a const T&: forward them
		// explicitly so an existing `LOGERR << ... << std::endl` keeps compiling. The manipulator appends to the buffered
		// message (the worker writes the trace footer after it).
		TracingErrorLine& operator<<(std::ostream& (*manip)(std::ostream&))
		{
			m_message << manip;
			return *this;
		}

	private:
		std::ostringstream m_prefix;              ///< the "[ts] [tag] [ERROR] [file:line fn]  " lead-in.
		std::ostringstream m_message;             ///< the streamed message body.
		std::string        m_externalFooter;      ///< a caller-supplied origin footer (set by withExternalTrace).
		bool               m_hasExternalFooter = false;    ///< true when m_externalFooter replaces the local stack capture.
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
