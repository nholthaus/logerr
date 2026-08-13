//--------------------------------------------------------------------------------------------------
//
//	ASYNC TRACE LOG
//
//--------------------------------------------------------------------------------------------------
//
// The MIT License (MIT)
//
// Copyright (c) 2026 Nic Holthaus
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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
//--------------------------------------------------------------------------------------------------
//
/// @file	asyncTraceLog.h
/// @brief	Off-thread symbolization of LOGERR stack traces.
/// @details
///		A LOGERR footer's stack trace is symbolized on a background worker, not on the thread that
///		logged the error. The logging thread only captures the raw return addresses (microseconds)
///		and hands them off; it never blocks on the tens-of-milliseconds symbol resolution. The
///		worker writes the WHOLE entry - the error line and its trace footer - as one atomic unit, so
///		message and trace are always contiguous in the log and no error line ever waits on its own
///		trace. This keeps an error on a latency-sensitive thread (a GUI thread, a worker loop that
///		must then emit a signal) from stalling on symbolization.
//
//--------------------------------------------------------------------------------------------------

#ifndef logerr_asyncTraceLog_h_
#define logerr_asyncTraceLog_h_

#include <string>
#include <vector>

namespace logerr
{
	/// @brief		Enqueue a deferred, to-be-symbolized error entry for the background trace-log worker.
	/// @details	The calling thread captures the raw return addresses (cheap) and passes them here; the worker
	///				symbolizes them off-thread and writes the complete entry (prefix + message, then the trace footer
	///				when it is a first-seen stack) atomically to std::cout. The call never blocks on symbolization.
	/// @param[in]	prefix				the already-formatted "[ts] [tag] [ERROR] [file:line fn]  " lead-in.
	/// @param[in]	message				the streamed message body for this error line.
	/// @param[in]	frames				the raw return addresses captured at the log site (CaptureStackBackTrace /
	///									backtrace output); symbolized by the worker.
	/// @param[in]	deduplicateByStack	when true, an identical stack already logged this process is written message-only
	///									(no trace footer); a distinct stack always gets its full trace.
	void enqueueTracedError(std::string prefix, std::string message, std::vector<void*> frames, bool deduplicateByStack);

	/// @brief		Synchronously drain every pending trace entry and stop the worker.
	/// @details	Called at process exit so no error entry is lost, and by the fatal-crash handler BEFORE it exits the
	///				process (an async worker would not otherwise drain before std::exit/abort). Safe to call more than
	///				once and safe to call when no entry was ever enqueued (the worker is started lazily on first use).
	void flushTracedErrors() noexcept;
}    // namespace logerr

#endif    // logerr_asyncTraceLog_h_
