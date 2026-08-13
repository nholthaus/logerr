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

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <functional>
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
		std::string           prefix;
		std::string           message;
		std::vector<void*>    frames;
		bool                  deduplicateByStack = false;
		// A flush barrier: when set, the worker invokes this instead of writing an entry, signaling a flush() waiter that
		// everything queued ahead of it has been processed. Empty for ordinary error entries.
		std::function<void()> barrier;
	};

	//----------------------------------------------------------------------------------------------------------------------
	//      FUNCTION: writeEntry [static]
	//----------------------------------------------------------------------------------------------------------------------
	/// @brief		Symbolize one entry's frames and write the whole entry atomically to std::cout.
	/// @param[in]	entry	the entry to symbolize and write.
	/// @details	Runs the deduplication gate against the provided frames and formats the footer only for a first-seen
	///				stack. The prefix + message and the footer are written under one mutex so entries stay contiguous. Used
	///				by the background worker and, during process teardown, by the synchronous fallback in enqueueTracedError.
	//----------------------------------------------------------------------------------------------------------------------
	void writeEntry(const TracedError& entry)
	{
		std::string footer;
		const bool  firstSeen = !entry.deduplicateByStack ||
		                       StackTrace::firstTimeForStack(entry.frames.data(), static_cast<int>(entry.frames.size()));
		if (firstSeen)
			footer = StackTrace::formatFrames(entry.frames.data(), static_cast<int>(entry.frames.size()));

		static std::mutex                 outputMutex;
		const std::lock_guard<std::mutex> lock(outputMutex);
		std::cout << entry.prefix << entry.message;
		if (!footer.empty())
			std::cout << '\n' << footer;
		std::cout << std::endl;
	}

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

		//----------------------------------------------------------------------------------------------------------------------
		//      FUNCTION: flush [public]
		//----------------------------------------------------------------------------------------------------------------------
		/// @brief		Block until the worker has drained every entry queued so far.
		/// @details	Enqueues a barrier the worker signals once it has processed everything ahead of it, then waits on
		///				that barrier. The worker keeps running afterward (the singleton is not torn down), so a later LOGERR
		///				is still served asynchronously. Used by the per-statement test flush and the crash handler.
		//----------------------------------------------------------------------------------------------------------------------
		void flush()
		{
			std::mutex              barrierMutex;
			std::condition_variable barrierDone;
			bool                    done = false;
			m_queue.push(TracedError{"", "", {}, false, [&]
			                         {
				                         const std::lock_guard<std::mutex> lock(barrierMutex);
				                         done = true;
				                         barrierDone.notify_one();
			                         }});
			std::unique_lock<std::mutex> lock(barrierMutex);
			barrierDone.wait(lock, [&] { return done; });
		}

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
			{
				if (entry.barrier)
					entry.barrier();    // a flush() barrier: signal the waiter, write nothing
				else
					writeEntry(entry);
			}
		}

		concurrent_queue<TracedError> m_queue;
		logerr::thread                m_thread;
	};

	// True once the worker singleton has begun (or finished) destruction at process exit. After this point a LOGERR must
	// NOT touch the worker (its thread is joined and its members are being torn down) and must NOT start a new one; it
	// writes synchronously instead. std::atomic so the check is race-free against the exit-time destructor.
	std::atomic_bool g_shuttingDown{false};

	// A holder whose destructor sets the shutdown flag. It is declared AFTER the worker in worker() below, so it is
	// CONSTRUCTED after the worker and therefore DESTROYED before it: the flag flips true first, then the worker is
	// stopped+joined. A LOGERR racing exit thus sees g_shuttingDown before the worker begins tearing down and takes the
	// synchronous path (never resurrecting the already-dying Meyers singleton, never starting a thread during teardown).
	struct ShutdownGuard
	{
		~ShutdownGuard() { g_shuttingDown.store(true); }
	};

	//----------------------------------------------------------------------------------------------------------------------
	//      FUNCTION: worker [static]
	//----------------------------------------------------------------------------------------------------------------------
	/// @brief		The process-lifetime trace-log worker singleton.
	/// @return		the single worker, constructed on first use.
	/// @details	A Meyers singleton: constructed on the first LOGERR and destroyed at process exit in REVERSE order of
	///				construction - before the C runtime's later teardown (including the coverage runtime's counter dump).
	///				Its destructor requests stop and JOINS the worker thread, so no background symbolization is in flight
	///				once the process tears down (the fix for a worker racing the gcov counter dump). The ShutdownGuard is
	///				declared AFTER the worker so it is destroyed BEFORE it, flipping g_shuttingDown before the worker dies.
	//----------------------------------------------------------------------------------------------------------------------
	TraceLogWorker& worker()
	{
		static TraceLogWorker instance;
		static ShutdownGuard  guard;    // constructed after instance -> destroyed before it: sets the flag, then worker joins
		return instance;
	}

	std::mutex g_workerMutex;
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
	/// @details	Ordinarily hands the entry to the background worker (constructed lazily on first use) and returns
	///				immediately - it never symbolizes or blocks on the calling thread. During process teardown, once the
	///				worker singleton is being destroyed (g_shuttingDown), the worker is gone / its thread joined, so the
	///				entry is symbolized and written SYNCHRONOUSLY on the calling thread instead - correct because there is
	///				no longer any concurrency to protect against and no worker to hand off to.
	//----------------------------------------------------------------------------------------------------------------------
	void enqueueTracedError(std::string prefix, std::string message, std::vector<void*> frames, bool deduplicateByStack)
	{
		TracedError entry{std::move(prefix), std::move(message), std::move(frames), deduplicateByStack, {}};
		if (g_shuttingDown.load())
		{
			// The worker singleton is torn down (or being torn down) at process exit: do not touch or resurrect it.
			writeEntry(entry);
			return;
		}
		const std::lock_guard<std::mutex> lock(g_workerMutex);
		worker().enqueue(std::move(entry));
	}

	//----------------------------------------------------------------------------------------------------------------------
	//      FUNCTION: flushTracedErrors [public]
	//----------------------------------------------------------------------------------------------------------------------
	/// @brief		Block until the worker has written every entry enqueued so far.
	/// @details	Waits on a barrier the worker processes in FIFO order behind the pending entries, so on return every
	///				earlier LOGERR has been symbolized and written. The worker keeps running (the singleton is not torn
	///				down), so later logging remains asynchronous. Called by the crash handler before it exits, and by tests
	///				that capture std::cout. A no-op once teardown has begun (no worker to flush) and when nothing was ever
	///				logged (no worker constructed yet).
	//----------------------------------------------------------------------------------------------------------------------
	void flushTracedErrors() noexcept
	{
		if (g_shuttingDown.load())
			return;
		const std::lock_guard<std::mutex> lock(g_workerMutex);
		worker().flush();
	}
}    // namespace logerr
