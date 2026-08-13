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

//----------------------------
//  INCLUDES
//----------------------------

#include <asyncTraceLog.h>

#include <StackTrace.h>
#include <concurrent_queue.h>
#include <logerrThread.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace
{
	// One deferred error entry: the already-formatted lead-in, the streamed message body, the raw return addresses to
	// symbolize on the worker, and whether an identical stack should be suppressed (message-only). The frames are
	// symbolized by the worker, never by the enqueuing thread.
	struct TracedError
	{
		std::string        prefix;
		std::string        message;
		std::vector<void*> frames;
		bool               deduplicateByStack = false;
	};

	// The process-lifetime worker. A single background thread drains the queue, symbolizes each entry's frames off the
	// logging thread, and writes the whole entry (prefix + message, then the trace footer) as one unit under a mutex so
	// entries never interleave. The thread is a logerr::thread (a std::jthread that catches escaping exceptions), so its
	// stop_token is the sole exit signal: concurrent_queue::wait_pop returns queued data even after stop is requested,
	// so requesting stop and joining drains everything accepted before shutdown.
	class TraceLogWorker
	{
	public:
		//----------------------------------------------------------------------------------------------------------------------
		//      FUNCTION: TraceLogWorker [public]
		//----------------------------------------------------------------------------------------------------------------------
		/// @brief		Start the background trace-log worker.
		/// @details	The worker blocks on the queue and drains it until its stop_token is requested and the queue is
		///				empty, symbolizing and writing each entry.
		//----------------------------------------------------------------------------------------------------------------------
		TraceLogWorker()
		    : m_thread([this](std::stop_token stop) { run(std::move(stop)); })
		{
		}

		//----------------------------------------------------------------------------------------------------------------------
		//      FUNCTION: ~TraceLogWorker [public]
		//----------------------------------------------------------------------------------------------------------------------
		/// @brief		Stop and join the worker, draining every remaining entry first.
		//----------------------------------------------------------------------------------------------------------------------
		~TraceLogWorker()
		{
			m_thread.request_stop();
			if (m_thread.joinable())
				m_thread.join();
		}

		TraceLogWorker(const TraceLogWorker&)            = delete;
		TraceLogWorker& operator=(const TraceLogWorker&) = delete;

		//----------------------------------------------------------------------------------------------------------------------
		//      FUNCTION: enqueue [public]
		//----------------------------------------------------------------------------------------------------------------------
		/// @brief		Hand a deferred error entry to the worker.
		/// @param[in]	entry	the entry to symbolize and write off-thread.
		//----------------------------------------------------------------------------------------------------------------------
		void enqueue(TracedError entry) { m_queue.push(std::move(entry)); }

	private:
		//----------------------------------------------------------------------------------------------------------------------
		//      FUNCTION: run [private]
		//----------------------------------------------------------------------------------------------------------------------
		/// @brief		The worker body: drain, symbolize, and write each entry until stopped and empty.
		/// @param[in]	stop	the worker's stop token; requesting it drains the remaining queue then exits the loop.
		//----------------------------------------------------------------------------------------------------------------------
		void run(std::stop_token stop)
		{
			TracedError entry;
			while (m_queue.wait_pop(entry, stop))
				write(entry);
		}

		//----------------------------------------------------------------------------------------------------------------------
		//      FUNCTION: write [private, static]
		//----------------------------------------------------------------------------------------------------------------------
		/// @brief		Symbolize one entry's frames and write the whole entry atomically to std::cout.
		/// @param[in]	entry	the entry to symbolize and write.
		/// @details	Runs the deduplication gate against the provided frames on the worker (so an identical stack is
		///				still suppressed off the logging thread) and formats the footer only for a first-seen stack. The
		///				prefix + message and the footer are written under one mutex so entries stay contiguous.
		//----------------------------------------------------------------------------------------------------------------------
		static void write(const TracedError& entry)
		{
			std::string footer;
			const bool  firstSeen = !entry.deduplicateByStack ||
			                       StackTrace::firstTimeForStack(entry.frames.data(), static_cast<int>(entry.frames.size()));
			if (firstSeen)
				footer = StackTrace::formatFrames(entry.frames.data(), static_cast<int>(entry.frames.size()));

			static std::mutex           outputMutex;
			const std::lock_guard<std::mutex> lock(outputMutex);
			std::cout << entry.prefix << entry.message;
			if (!footer.empty())
				std::cout << '\n' << footer;
			std::cout << std::endl;
		}

		concurrent_queue<TracedError> m_queue;
		logerr::thread                m_thread;
	};

	// The lazily-started worker and the lock that guards its lifetime. The worker is a raw owning pointer rather than a
	// function-local static so flushTracedErrors() can tear it down (join) and a later enqueue can start a fresh one -
	// a function-local static could only be destroyed once, at process exit, which would defeat an explicit flush.
	std::mutex                      g_workerMutex;
	std::unique_ptr<TraceLogWorker> g_worker;
	bool                            g_atexitRegistered = false;
}    // namespace

namespace logerr
{
	//----------------------------------------------------------------------------------------------------------------------
	//      FUNCTION: enqueueTracedError [public]
	//----------------------------------------------------------------------------------------------------------------------
	/// @brief		Enqueue a deferred, to-be-symbolized error entry for the background trace-log worker.
	/// @param[in]	prefix				the already-formatted "[ts] [tag] [ERROR] [file:line fn]  " lead-in.
	/// @param[in]	message				the streamed message body for this error line.
	/// @param[in]	frames				the raw return addresses captured at the log site; symbolized by the worker.
	/// @param[in]	deduplicateByStack	when true, an identical already-logged stack is written message-only.
	/// @details	Starts the worker lazily on first use and registers flushTracedErrors() with std::atexit exactly once
	///				so nothing is lost at normal process exit. The call never symbolizes and never blocks on symbol
	///				resolution; it only captures the entry and wakes the worker.
	//----------------------------------------------------------------------------------------------------------------------
	void enqueueTracedError(std::string prefix, std::string message, std::vector<void*> frames, bool deduplicateByStack)
	{
		const std::lock_guard<std::mutex> lock(g_workerMutex);
		if (!g_worker)
			g_worker = std::make_unique<TraceLogWorker>();
		if (!g_atexitRegistered)
		{
			std::atexit([] { flushTracedErrors(); });
			g_atexitRegistered = true;
		}
		g_worker->enqueue(TracedError{std::move(prefix), std::move(message), std::move(frames), deduplicateByStack});
	}

	//----------------------------------------------------------------------------------------------------------------------
	//      FUNCTION: flushTracedErrors [public]
	//----------------------------------------------------------------------------------------------------------------------
	/// @brief		Synchronously drain every pending trace entry and stop the worker.
	/// @details	Destroying the worker requests its stop_token and joins it; the queue's wait_pop returns every entry
	///				accepted before shutdown, so the join blocks until all remaining entries are symbolized and written.
	///				Safe to call more than once (a no-op when there is no worker) and safe when no entry was ever enqueued.
	//----------------------------------------------------------------------------------------------------------------------
	void flushTracedErrors() noexcept
	{
		std::unique_ptr<TraceLogWorker> worker;
		{
			const std::lock_guard<std::mutex> lock(g_workerMutex);
			worker = std::move(g_worker);
		}
		// The worker's destructor stops and joins the thread outside the lock, so a concurrent enqueue is never blocked
		// on the drain and a re-entrant flush observes an empty g_worker.
		worker.reset();
	}
}    // namespace logerr
