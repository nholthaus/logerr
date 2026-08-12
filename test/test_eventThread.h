#pragma once

// ---------------------------------------------------------------------------------------------------------------------
//			                 __
//			               _/ /          /
//			    __________/  /__ _______/
//			   /  _______   ___//  ____/
//			  /  /___   /  /   /  /   /   SYSTEMS
//			  \____  \ /  /   /  /   /    & TECHNOLOGY
//			 _____/  //  /___/  /   /     RESEARCH
//			/_______/ \________/   /
//			                      /
//			                     /
// ---------------------------------------------------------------------------------------------------------------------
//
/// @file       eventThreadTest.cpp
/// @author     Nic Holthaus
/// @date       8/1/2026
/// @copyright  (c) 2026 STR.
//
// ---------------------------------------------------------------------------------------------------------------------
//
/// @brief      Unit tests for `QEventThread` and `QNotifyingQueue` — the composable jthread + private-QEventLoop worker
///             and its reactive queue. A foundational reuse primitive, covered to the corners: the reactive queue's two
///             notification channels (onPush callback + CV wait) and its drain consumer; the worker's startup
///             handshake; the INPUT path (enqueue / ON_DATA_RECEIVED / one-lane submission ordering, threading,
///             coalescing); the OUTPUT path (EMIT / onOutput / outputs() / wait_pop_for); the callable runOnThread lane;
///             born-on-worker RAII (create AND destroy on the worker); the stop_token teardown SSOT + its races; and
///             exception capture on every guarded path.
//
// ---------------------------------------------------------------------------------------------------------------------

#include "QEventThread.h"

#include <QCoreApplication>
#include <QObject>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <semaphore>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace test_eventThread
{
	using namespace std::chrono_literals;

	class QNotifyingQueueFixture : public ::testing::Test {};
	class QEventThreadFixture : public ::testing::Test {};

	// NOTE: no Q_OBJECT type is declared in this fixture header — AUTOMOC would double-compile it into helenTest and
	// LNK2005 (the standing test-double rule). Cross-thread effects are observed via atomics + waitFor.

	// Spin (event-driven) until @p pred holds or a generous timeout elapses; the worker is a separate thread, so we only
	// wait for its effect — no processEvents pump on THIS thread. Returns pred().
	template<typename Pred>
	bool waitFor(Pred pred, int timeoutMs = 2000)
	{
		for (int i = 0; i < timeoutMs && !pred(); ++i)
			QThread::msleep(1);
		return pred();
	}

	// A QObject that flips an EXTERNAL atomic in its destructor — proves WHEN and ON WHICH THREAD a worker-owned object
	// is reaped (born-on-worker RAII). Plain QObject (no signals) → moc-free here.
	class DtorProbe : public QObject
	{
	public:
		DtorProbe(std::atomic<QThread*>* on, std::atomic_int* count, QObject* parent)
		    : QObject(parent), m_on(on), m_count(count) {}
		~DtorProbe() override
		{
			if (m_on) m_on->store(QThread::currentThread());
			if (m_count) m_count->fetch_add(1, std::memory_order_relaxed);
		}
	private:
		std::atomic<QThread*>* m_on    = nullptr;
		std::atomic_int*       m_count = nullptr;
	};

	//======================================================================================================================
	//      QNotifyingQueue — the reactive queue underneath, exercised standalone
	//======================================================================================================================

	TEST_F(QNotifyingQueueFixture, PushAndTryPopFifo)
	{
		QNotifyingQueue<int> q;
		q.push(1);
		q.push(2);
		q.emplace(3);
		EXPECT_EQ(q.size(), 3u);
		EXPECT_FALSE(q.empty());
		int v = -1;
		ASSERT_TRUE(q.try_pop(v));
		EXPECT_EQ(v, 1);
		ASSERT_TRUE(q.try_pop(v));
		EXPECT_EQ(v, 2);
		ASSERT_TRUE(q.try_pop(v));
		EXPECT_EQ(v, 3);
		EXPECT_FALSE(q.try_pop(v));    // empty now
		EXPECT_TRUE(q.empty());
	}

	TEST_F(QNotifyingQueueFixture, OnPushFiresOncePerPushOnThePushingThread)
	{
		QNotifyingQueue<int> q;
		std::atomic_int       fires{0};
		std::atomic<QThread*> firedOn{nullptr};
		q.onPush([&] { fires.fetch_add(1, std::memory_order_relaxed); firedOn.store(QThread::currentThread()); });
		q.push(1);
		q.emplace(2);
		int v; q.push(3);
		(void) v;
		EXPECT_EQ(fires.load(), 3);
		EXPECT_EQ(firedOn.load(), QThread::currentThread());    // callback runs on the producer's thread
	}

	TEST_F(QNotifyingQueueFixture, NoOnPushHandlerIsHarmless)
	{
		QNotifyingQueue<int> q;    // never call onPush
		q.push(1);
		q.emplace(2);
		int v;
		EXPECT_TRUE(q.try_pop(v));
		SUCCEED();
	}

	TEST_F(QNotifyingQueueFixture, ClearEmptiesTheQueue)
	{
		QNotifyingQueue<int> q;
		for (int i = 0; i < 5; ++i) q.push(i);
		EXPECT_EQ(q.size(), 5u);
		q.clear();
		EXPECT_TRUE(q.empty());
		int v;
		EXPECT_FALSE(q.try_pop(v));
	}

	TEST_F(QNotifyingQueueFixture, WaitPopForBlocksUntilAnItemArrives)
	{
		QNotifyingQueue<int> q;
		int                  got = -1;
		// A producer pushes after a short delay; the CV channel wakes the blocking consumer.
		std::thread producer([&] { QThread::msleep(20); q.push(99); });
		const bool  ok = q.wait_pop_for(got, 2000ms);
		producer.join();
		EXPECT_TRUE(ok);
		EXPECT_EQ(got, 99);
	}

	TEST_F(QNotifyingQueueFixture, WaitPopForTimesOutWhenEmpty)
	{
		QNotifyingQueue<int> q;
		int                  got = -1;
		EXPECT_FALSE(q.wait_pop_for(got, 20ms));    // nothing pushed → timeout
	}

	TEST_F(QNotifyingQueueFixture, DrainInvokesTheConsumerFifoWithTheWrapper)
	{
		QNotifyingQueue<int> q;
		std::vector<int>     seen;
		int                  wraps = 0;
		q.onDataReceived([&](int& x) { seen.push_back(x); });
		for (int i = 0; i < 4; ++i) q.push(i);
		q.drain([&](auto&& w) { ++wraps; w(); });    // the wrapper is applied around each item
		ASSERT_EQ(seen.size(), 4u);
		for (int i = 0; i < 4; ++i) EXPECT_EQ(seen[i], i);
		EXPECT_EQ(wraps, 4);
		EXPECT_TRUE(q.empty());
	}

	TEST_F(QNotifyingQueueFixture, DrainWithNoConsumerIsANoOpAndLeavesItems)
	{
		QNotifyingQueue<int> q;
		q.push(1);
		q.push(2);
		int wraps = 0;
		q.drain([&](auto&& w) { ++wraps; w(); });    // no consumer registered
		EXPECT_EQ(wraps, 0);
		EXPECT_EQ(q.size(), 2u);    // items untouched
	}

	TEST_F(QNotifyingQueueFixture, MoveOnlyElementRoundTrips)
	{
		QNotifyingQueue<std::unique_ptr<int>> q;
		q.push(std::make_unique<int>(5));
		q.emplace(std::make_unique<int>(6));
		std::unique_ptr<int> got;
		ASSERT_TRUE(q.try_pop(got));
		ASSERT_TRUE(got != nullptr);
		EXPECT_EQ(*got, 5);
	}

	//======================================================================================================================
	//      STARTUP HANDSHAKE  (data-less worker)
	//======================================================================================================================

	TEST_F(QEventThreadFixture, SetupRunsExactlyOnceOnAWorkerThreadDistinctFromCaller)
	{
		std::atomic_int       setupCount{0};
		std::atomic<QThread*> setupThread{nullptr};
		{
			QEventThread<> worker = THREAD_SETUP_CAPTURE([&], {
				setupCount.fetch_add(1, std::memory_order_relaxed);
				setupThread.store(QThread::currentThread());
			});
		}
		EXPECT_EQ(setupCount.load(), 1);
		EXPECT_NE(setupThread.load(), nullptr);
		EXPECT_NE(setupThread.load(), QThread::currentThread());
	}

	//======================================================================================================================
	//      NON-BLOCKING CONSTRUCTION — the ctor returns before the loop is live; a pre-loop submission is not lost
	//======================================================================================================================

	TEST_F(QEventThreadFixture, NonBlockingCtorReturnsBeforeSetupRunsButPreLoopWorkStillRuns)
	{
		// StartMode::NonBlocking: the ctor does NOT wait for the loop. Work submitted IMMEDIATELY after construction (a
		// window in which the loop may not be live yet) must still run — buffered in the submission lane, drained the
		// instant the loop comes up. This is the #191/#207 perf mode (no per-object ctor block) with no lost work.
		std::atomic_int       ran{0};
		std::atomic_bool      setupStarted{false};
		std::atomic<QThread*> ranOn{nullptr};
		{
			QEventThread<> worker(
			    THREAD_SETUP_CAPTURE([&], { setupStarted.store(true); }), QEventThread<>::StartMode::NonBlocking);
			// Submit at once — likely before the loop is live. It must be buffered and run once the loop comes up.
			for (int i = 0; i < 50; ++i)
				worker.runOnThread([&] { ranOn.store(QThread::currentThread()); ran.fetch_add(1, std::memory_order_relaxed); });
			EXPECT_TRUE(waitFor([&] { return ran.load() == 50; }));
		}
		EXPECT_EQ(ran.load(), 50);                                  // every pre-loop submission ran (none dropped)
		EXPECT_TRUE(setupStarted.load());
		EXPECT_NE(ranOn.load(), QThread::currentThread());          // ran on the worker
	}

	TEST_F(QEventThreadFixture, NonBlockingSubmittedWorkNeverRunsBeforeSetupCreatedItsState)
	{
		// REGRESSION (the Phase-Q coverage SEGFAULT): a submitted closure must NEVER be drained before the setup body has
		// run and created the worker-owned state that closure touches. In NonBlocking mode a submission's onPush→wake()
		// posts a queued drainInput that can be serviced BEFORE the setup singleShot fires; draining then invoked an owner
		// closure (LivenessProbe::disarmTimers) against a not-yet-created QTimer — a null-deref crash. Setup here creates a
		// heap "resource"; every submitted closure asserts the resource EXISTS when it runs. The drain gate makes that hold
		// under the race. Looped so the construct→submit→loop-comes-up interleaving is exercised many times.
		for (int rep = 0; rep < 200; ++rep)
		{
			std::atomic<int*> resource{nullptr};    // "created by setup"; a closure that runs pre-setup would see nullptr
			std::atomic_int   ran{0};
			std::atomic_bool  sawNullBeforeSetup{false};
			{
				QEventThread<> worker(
				    THREAD_SETUP_CAPTURE([&], { resource.store(new int(42), std::memory_order_release); }),
				    QEventThread<>::StartMode::NonBlocking);
				// Submit at once — the wake races the setup singleShot. Each closure must see the setup-created resource.
				for (int i = 0; i < 8; ++i)
					worker.runOnThread([&] {
						if (!resource.load(std::memory_order_acquire))
							sawNullBeforeSetup.store(true);    // a drain beat setup — the exact crash precondition
						ran.fetch_add(1, std::memory_order_relaxed);
					});
				EXPECT_TRUE(waitFor([&] { return ran.load() == 8; }));
			}
			EXPECT_FALSE(sawNullBeforeSetup.load()) << "a submitted closure drained before setup created its state (rep " << rep << ")";
			delete resource.load();
		}
	}

	TEST_F(QEventThreadFixture, NonBlockingFastConstructThenDestroyDoesNotHang)
	{
		// The pre-loop-stop path is REACHABLE in NonBlocking mode (the ctor returns before the loop is live, so destroy
		// can request stop first). It must still join cleanly — no hang. Stress it.
		for (int i = 0; i < 200; ++i)
			QEventThread<> worker(THREAD_SETUP_CAPTURE([&], {}), QEventThread<>::StartMode::NonBlocking);
		SUCCEED();
	}

	TEST_F(QEventThreadFixture, NonBlockingInputWorkerDeliversPreLoopEnqueuesInOrder)
	{
		// A NonBlocking input worker: data enqueued before the loop is live is delivered to the handler, in order.
		std::vector<int> seen;
		std::atomic_int  done{0};
		{
			QEventThread<int> worker(
			    THREAD_SETUP_CAPTURE([&], {
				    ON_DATA_RECEIVED_CAPTURE([&], { seen.push_back(data); done.fetch_add(1, std::memory_order_relaxed); });
			    }),
			    QEventThread<int>::StartMode::NonBlocking);
			for (int i = 0; i < 40; ++i)
				worker.enqueue(i);
			EXPECT_TRUE(waitFor([&] { return done.load() == 40; }));
		}
		ASSERT_EQ(static_cast<int>(seen.size()), 40);
		for (int i = 0; i < 40; ++i)
			EXPECT_EQ(seen[i], i) << "pre-loop enqueue delivered out of order at " << i;
	}

	TEST_F(QEventThreadFixture, IsRunningIsTrueWhileAliveAndTheThreadIsJoinable)
	{
		auto worker = std::make_unique<QEventThread<>>(THREAD_SETUP_CAPTURE([&], {}));
		EXPECT_TRUE(worker->isRunning());
		worker.reset();
		SUCCEED();
	}

	//======================================================================================================================
	//      runOnThread — the callable lane (data-less worker)
	//======================================================================================================================

	TEST_F(QEventThreadFixture, RunOnThreadRunsWorkOnTheWorkerThread)
	{
		std::atomic<QThread*> workerThread{nullptr};
		std::atomic<QThread*> ranOn{nullptr};
		std::atomic_int       count{0};
		{
			QEventThread<> worker = THREAD_SETUP_CAPTURE([&], { workerThread.store(QThread::currentThread()); });
			worker.runOnThread([&] {
				ranOn.store(QThread::currentThread());
				count.fetch_add(1, std::memory_order_relaxed);
			});
			EXPECT_TRUE(waitFor([&] { return count.load() == 1; }));
		}
		EXPECT_EQ(count.load(), 1);
		EXPECT_EQ(ranOn.load(), workerThread.load());
		EXPECT_NE(ranOn.load(), QThread::currentThread());
	}

	TEST_F(QEventThreadFixture, RunOnThreadPreservesFifoOrder)
	{
		std::vector<int> order;
		std::atomic_int  done{0};
		{
			QEventThread<> worker = THREAD_SETUP_CAPTURE([&], {});
			for (int i = 0; i < 100; ++i)
				worker.runOnThread([&order, &done, i] { order.push_back(i); done.fetch_add(1, std::memory_order_relaxed); });
			EXPECT_TRUE(waitFor([&] { return done.load() == 100; }));
		}
		ASSERT_EQ(static_cast<int>(order.size()), 100);
		for (int i = 0; i < 100; ++i)
			EXPECT_EQ(order[i], i) << "runOnThread ran out of order at " << i;
	}

	TEST_F(QEventThreadFixture, RunOnThreadAfterStopIsANoOp)
	{
		auto worker = std::make_unique<QEventThread<>>(THREAD_SETUP_CAPTURE([&], {}));
		worker->requestStop();
		QThread::msleep(20);
		worker->runOnThread([] { FAIL() << "must not run after stop"; });
		SUCCEED();
	}

	TEST_F(QEventThreadFixture, RunOnThreadWorksOnAnInputWorkerViaItsOwnLane)
	{
		// On an input worker (In != void) runOnThread rides a SEPARATE callable lane, not the typed input queue.
		std::atomic_int dataSeen{0};
		std::atomic_int callableRan{0};
		{
			QEventThread<int> worker = THREAD_SETUP_CAPTURE([&], {
				ON_DATA_RECEIVED_CAPTURE([&], { (void) data; dataSeen.fetch_add(1, std::memory_order_relaxed); });
			});
			worker.enqueue(1);
			worker.runOnThread([&] { callableRan.fetch_add(1, std::memory_order_relaxed); });
			worker.enqueue(2);
			EXPECT_TRUE(waitFor([&] { return dataSeen.load() == 2 && callableRan.load() == 1; }));
		}
		EXPECT_EQ(dataSeen.load(), 2);
		EXPECT_EQ(callableRan.load(), 1);
	}

	//======================================================================================================================
	//      INPUT path — enqueue + ON_DATA_RECEIVED (the networking sweet spot)
	//======================================================================================================================

	TEST_F(QEventThreadFixture, EnqueuedDataIsDeliveredToTheHandlerOnTheWorkerInOrder)
	{
		std::vector<int>      seen;
		std::atomic<QThread*> handlerThread{nullptr};
		std::atomic_int       done{0};
		{
			QEventThread<int> worker = THREAD_SETUP_CAPTURE([&], {
				ON_DATA_RECEIVED_CAPTURE([&], {
					handlerThread.store(QThread::currentThread());
					seen.push_back(data);
					done.fetch_add(1, std::memory_order_relaxed);
				});
			});
			for (int i = 0; i < 50; ++i)
				worker.enqueue(i);
			EXPECT_TRUE(waitFor([&] { return done.load() == 50; }));
		}
		ASSERT_EQ(static_cast<int>(seen.size()), 50);
		for (int i = 0; i < 50; ++i)
			EXPECT_EQ(seen[i], i) << "data delivered out of order at " << i;
		EXPECT_NE(handlerThread.load(), QThread::currentThread());    // handled on the worker, not the producer
	}

	TEST_F(QEventThreadFixture, DataEnqueuedRightAfterConstructionIsNotLost)
	{
		std::atomic_int consumed{0};
		{
			QEventThread<int> worker = THREAD_SETUP_CAPTURE([&], {
				ON_DATA_RECEIVED_CAPTURE([&], { (void) data; consumed.fetch_add(1, std::memory_order_relaxed); });
			});
			for (int i = 0; i < 10; ++i)
				worker.enqueue(i);
			EXPECT_TRUE(waitFor([&] { return consumed.load() == 10; }));
		}
		EXPECT_EQ(consumed.load(), 10);
	}

	TEST_F(QEventThreadFixture, EnqueueIsThreadSafeFromManyProducers)
	{
		constexpr int producers = 8;
		constexpr int perThread = 100;
		std::atomic_int consumed{0};
		{
			QEventThread<int> worker = THREAD_SETUP_CAPTURE([&], {
				ON_DATA_RECEIVED_CAPTURE([&], { (void) data; consumed.fetch_add(1, std::memory_order_relaxed); });
			});
			std::vector<std::thread> threads;
			for (int t = 0; t < producers; ++t)
				threads.emplace_back([&worker, perThread] {
					for (int i = 0; i < perThread; ++i)
						worker.enqueue(i);
				});
			for (auto& th : threads) th.join();
			EXPECT_TRUE(waitFor([&] { return consumed.load() == producers * perThread; }, 5000));
		}
		EXPECT_EQ(consumed.load(), producers * perThread);    // every item consumed, none lost, no crash
	}

	TEST_F(QEventThreadFixture, EnqueueMovesTheItem)
	{
		std::atomic_bool got{false};
		{
			QEventThread<std::unique_ptr<int>> worker = THREAD_SETUP_CAPTURE([&], {
				ON_DATA_RECEIVED_CAPTURE([&], { if (data && *data == 7) got.store(true); });
			});
			worker.enqueue(std::make_unique<int>(7));
			EXPECT_TRUE(waitFor([&] { return got.load(); }));
		}
		EXPECT_TRUE(got.load());
	}

	TEST_F(QEventThreadFixture, EnqueueAndRunOnThreadShareOneLaneInSubmissionOrder)
	{
		// enqueue() (typed data) and runOnThread() (callables) ride ONE ordered lane, so they run in true SUBMISSION
		// order — a runOnThread issued before an enqueue runs before it, and vice versa. (Regression guard for the
		// cross-lane inversion the two-lane design had: a listen()-via-runOnThread then send()-via-enqueue must NOT
		// invert.) Record a single interleaved trace and assert it matches submission order exactly.
		std::vector<int> trace;    // data items recorded as their value; callables recorded as their negative marker
		std::atomic_int  seen{0};
		{
			QEventThread<int> worker = THREAD_SETUP_CAPTURE([&], {
				ON_DATA_RECEIVED_CAPTURE([&], { trace.push_back(data); seen.fetch_add(1, std::memory_order_relaxed); });
			});
			// interleave: run(-1), data(1), data(2), run(-2), data(3), run(-3)
			worker.runOnThread([&] { trace.push_back(-1); seen.fetch_add(1, std::memory_order_relaxed); });
			worker.enqueue(1);
			worker.enqueue(2);
			worker.runOnThread([&] { trace.push_back(-2); seen.fetch_add(1, std::memory_order_relaxed); });
			worker.enqueue(3);
			worker.runOnThread([&] { trace.push_back(-3); seen.fetch_add(1, std::memory_order_relaxed); });
			EXPECT_TRUE(waitFor([&] { return seen.load() == 6; }));
		}
		ASSERT_EQ(static_cast<int>(trace.size()), 6);
		const std::vector<int> expected = { -1, 1, 2, -2, 3, -3 };
		EXPECT_EQ(trace, expected) << "enqueue/runOnThread did not drain in submission order (cross-lane inversion)";
	}

	//======================================================================================================================
	//      OUTPUT path — EMIT + onOutput / outputs()  (full-duplex worker)
	//======================================================================================================================

	TEST_F(QEventThreadFixture, WorkerEmitsResultsThatTheOwnerDrainsFromOutputs)
	{
		{
			QEventThread<int, int> worker = THREAD_SETUP_CAPTURE([&], {
				ON_DATA_RECEIVED_CAPTURE([&], { EMIT(data * 10); });
			});
			for (int i = 0; i < 20; ++i)
				worker.enqueue(i);
			EXPECT_TRUE(waitFor([&] { return worker.outputs().size() == 20u; }, 5000));

			std::vector<int> results;
			int              v;
			while (worker.outputs().try_pop(v)) results.push_back(v);
			ASSERT_EQ(static_cast<int>(results.size()), 20);
			for (int i = 0; i < 20; ++i)
				EXPECT_EQ(results[i], i * 10) << "output out of order at " << i;
		}
	}

	TEST_F(QEventThreadFixture, EmitCanFireZeroOrManyPerInput)
	{
		// The emission trigger is the enqueue to `output`, so a handler may emit 0, 1, or N per input.
		{
			QEventThread<int, int> worker = THREAD_SETUP_CAPTURE([&], {
				ON_DATA_RECEIVED_CAPTURE([&], {
					if (data == 0) return;              // 0 outputs
					for (int k = 0; k < data; ++k) EMIT(data);    // N outputs
				});
			});
			worker.enqueue(0);    // emits nothing
			worker.enqueue(3);    // emits 3
			EXPECT_TRUE(waitFor([&] { return worker.outputs().size() == 3u; }));
			int v, n = 0;
			while (worker.outputs().try_pop(v)) { EXPECT_EQ(v, 3); ++n; }
			EXPECT_EQ(n, 3);
		}
	}

	TEST_F(QEventThreadFixture, EmitMovesAMoveOnlyResult)
	{
		{
			QEventThread<int, std::unique_ptr<int>> worker = THREAD_SETUP_CAPTURE([&], {
				ON_DATA_RECEIVED_CAPTURE([&], { EMIT(std::make_unique<int>(data + 1)); });
			});
			worker.enqueue(41);
			EXPECT_TRUE(waitFor([&] { return worker.outputs().size() == 1u; }));
			std::unique_ptr<int> got;
			ASSERT_TRUE(worker.outputs().try_pop(got));
			ASSERT_TRUE(got != nullptr);
			EXPECT_EQ(*got, 42);
		}
	}

	TEST_F(QEventThreadFixture, ABareThreadConsumerCanBlockOnOutputsViaWaitPopFor)
	{
		// The second output-notification channel: a plain consumer thread (no event loop) blocks on the CV.
		std::vector<int> got;
		{
			QEventThread<int, int> worker = THREAD_SETUP_CAPTURE([&], {
				ON_DATA_RECEIVED_CAPTURE([&], { EMIT(data + 1); });
			});
			std::thread consumer([&] {
				int v;
				while (got.size() < 5 && worker.outputs().wait_pop_for(v, 2000ms))
					got.push_back(v);
			});
			for (int i = 0; i < 5; ++i) worker.enqueue(i);
			consumer.join();
		}
		ASSERT_EQ(static_cast<int>(got.size()), 5);
		for (int i = 0; i < 5; ++i) EXPECT_EQ(got[i], i + 1);
	}

	TEST_F(QEventThreadFixture, OnOutputHandlerDeliversOnTheOwnerThread)
	{
		std::vector<int>      got;
		std::atomic<QThread*> deliverThread{nullptr};
		QThread*              ownerThread = QThread::currentThread();
		{
			QEventThread<int, int> worker = THREAD_SETUP_CAPTURE([&], {
				ON_DATA_RECEIVED_CAPTURE([&], { EMIT(data + 100); });
			});
			worker.onOutput([&](int& v) {
				got.push_back(v);
				deliverThread.store(QThread::currentThread());
			});
			for (int i = 0; i < 5; ++i)
				worker.enqueue(i);
			// Let the worker produce, then pump the owner loop so the queued onOutput drain fires on THIS thread.
			EXPECT_TRUE(waitFor([&] { return worker.outputs().size() + static_cast<size_t>(got.size()) >= 5; }));
			for (int i = 0; i < 200 && got.size() < 5; ++i)
				QCoreApplication::processEvents();
		}
		ASSERT_EQ(static_cast<int>(got.size()), 5);
		for (int i = 0; i < 5; ++i)
			EXPECT_EQ(got[i], i + 100);
		EXPECT_EQ(deliverThread.load(), ownerThread);    // delivered on the owner (this) thread, not the worker
	}

	TEST_F(QEventThreadFixture, OutputEmittedBeforeOnOutputIsRegisteredIsNotLost)
	{
		std::vector<int> got;
		{
			QEventThread<int, int> worker = THREAD_SETUP_CAPTURE([&], {
				ON_DATA_RECEIVED_CAPTURE([&], { EMIT(data); });
			});
			worker.enqueue(1);
			worker.enqueue(2);
			EXPECT_TRUE(waitFor([&] { return worker.outputs().size() == 2u; }));    // emitted before we register
			worker.onOutput([&](int& v) { got.push_back(v); });                     // drains what's already there
			for (int i = 0; i < 200 && got.size() < 2; ++i)
				QCoreApplication::processEvents();
		}
		ASSERT_EQ(static_cast<int>(got.size()), 2);
		EXPECT_EQ(got[0], 1);
		EXPECT_EQ(got[1], 2);
	}

	TEST_F(QEventThreadFixture, OutputOnlyWorkerPublishesWithoutAnInputLane)
	{
		QEventThread<void, int> worker = THREAD_SETUP_CAPTURE([&], {
			EMIT(10);
			EMIT(20);
		});

		ASSERT_EQ(worker.outputs().size(), 2u);
		int first = 0;
		int second = 0;
		ASSERT_TRUE(worker.outputs().try_pop(first));
		ASSERT_TRUE(worker.outputs().try_pop(second));
		EXPECT_EQ(first, 10);
		EXPECT_EQ(second, 20);
	}

	//======================================================================================================================
	//      coalescing + re-entrancy
	//======================================================================================================================

	TEST_F(QEventThreadFixture, ABurstIsDrainedInOrderByCoalescedWakes)
	{
		constexpr int    n = 500;
		std::vector<int> seen;
		std::atomic_int  done{0};
		{
			QEventThread<int> worker = THREAD_SETUP_CAPTURE([&], {
				ON_DATA_RECEIVED_CAPTURE([&], { seen.push_back(data); done.fetch_add(1, std::memory_order_relaxed); });
			});
			for (int i = 0; i < n; ++i) worker.enqueue(i);
			EXPECT_TRUE(waitFor([&] { return done.load() == n; }, 5000));
		}
		ASSERT_EQ(static_cast<int>(seen.size()), n);
		for (int i = 0; i < n; ++i) EXPECT_EQ(seen[i], i);
	}

	TEST_F(QEventThreadFixture, EnqueueFromWithinTheHandlerRidesTheNextDrain)
	{
		std::atomic_int  total{0};
		std::atomic_bool secondSeen{false};
		{
			QEventThread<int> worker = THREAD_SETUP_CAPTURE([&], {
				ON_DATA_RECEIVED_CAPTURE([&], {
					total.fetch_add(1, std::memory_order_relaxed);
					if (data == 1) { secondSeen.store(true); }
					else if (data == 0) { worker.enqueue(1); }    // re-enqueue from the worker itself
				});
			});
			worker.enqueue(0);
			EXPECT_TRUE(waitFor([&] { return secondSeen.load(); }));
		}
		EXPECT_TRUE(secondSeen.load());
		EXPECT_EQ(total.load(), 2);
	}

	//======================================================================================================================
	//      BORN ON THE WORKER — created AND reaped on the worker thread (the core RAII promise)
	//======================================================================================================================

	TEST_F(QEventThreadFixture, WorkerOwnedObjectsAreReapedOnTheWorkerThreadAtUnwind)
	{
		std::atomic<QThread*> destroyedOn{nullptr};
		std::atomic_int       destroyedCount{0};
		std::atomic<QThread*> workerThread{nullptr};
		{
			QEventThread<> worker = THREAD_SETUP_CAPTURE([&], {
				workerThread.store(QThread::currentThread());
				new DtorProbe(&destroyedOn, &destroyedCount, eventLoop);    // parented to the loop → reaped on unwind
			});
			EXPECT_EQ(destroyedCount.load(), 0);
		}
		EXPECT_EQ(destroyedCount.load(), 1);
		EXPECT_EQ(destroyedOn.load(), workerThread.load());
		EXPECT_NE(destroyedOn.load(), QThread::currentThread());
	}

	TEST_F(QEventThreadFixture, ManyWorkerObjectsAllReaped)
	{
		std::atomic_int destroyedCount{0};
		{
			QEventThread<> worker = THREAD_SETUP_CAPTURE([&], {
				for (int i = 0; i < 25; ++i) new DtorProbe(nullptr, &destroyedCount, eventLoop);
			});
		}
		EXPECT_EQ(destroyedCount.load(), 25);
	}

	TEST_F(QEventThreadFixture, ATimerCreatedInSetupFiresOnTheWorker)
	{
		std::atomic_int       ticks{0};
		std::atomic<QThread*> tickThread{nullptr};
		{
			QEventThread<> worker = THREAD_SETUP_CAPTURE([&], {
				auto* timer = new QTimer(eventLoop);
				QObject::connect(timer, &QTimer::timeout, eventLoop, [&] {
					tickThread.store(QThread::currentThread());
					ticks.fetch_add(1, std::memory_order_relaxed);
				});
				timer->start(2);
			});
			EXPECT_TRUE(waitFor([&] { return ticks.load() >= 3; }));
		}
		EXPECT_GE(ticks.load(), 3);
		EXPECT_NE(tickThread.load(), QThread::currentThread());
	}

	//======================================================================================================================
	//      TEARDOWN — stop_token SSOT, join barrier, no-hang under every race
	//======================================================================================================================

	TEST_F(QEventThreadFixture, DestructorStopsAndJoinsWithoutHanging)
	{
		std::atomic_bool ran{false};
		{
			QEventThread<> worker = THREAD_SETUP_CAPTURE([&], { ran.store(true); });
			EXPECT_TRUE(worker.isRunning());
		}
		EXPECT_TRUE(ran.load());
		SUCCEED();
	}

	TEST_F(QEventThreadFixture, DestructorStopsAWorkerBusyWithARepeatingTimer)
	{
		std::atomic_int ticks{0};
		{
			QEventThread<> worker = THREAD_SETUP_CAPTURE([&], {
				auto* timer = new QTimer(eventLoop);
				QObject::connect(timer, &QTimer::timeout, eventLoop, [&] { ticks.fetch_add(1, std::memory_order_relaxed); });
				timer->start(1);
			});
			EXPECT_TRUE(waitFor([&] { return ticks.load() >= 5; }));
		}
		const int settled = ticks.load();
		QThread::msleep(30);
		EXPECT_EQ(ticks.load(), settled);    // no ticks after join — the worker really stopped
	}

	TEST_F(QEventThreadFixture, FastCreateThenDestroyDoesNotHang)
	{
		for (int i = 0; i < 200; ++i)
			QEventThread<> worker = THREAD_SETUP_CAPTURE([&], {});
		SUCCEED();
	}

	TEST_F(QEventThreadFixture, RequestStopBeforeTheLoopIsLiveStillJoins)
	{
		// Stress the pre-loop stop race: construct and immediately request stop, many times. The stop_callback must quit
		// the loop synchronously even if stop beat exec(), so the dtor join never hangs.
		for (int i = 0; i < 200; ++i)
		{
			QEventThread<> worker = THREAD_SETUP_CAPTURE([&], {});
			worker.requestStop();
		}
		SUCCEED();
	}

	TEST_F(QEventThreadFixture, DataWorkerCreateDestroyWithEnqueuesInterleavedNoHangOrLeak)
	{
		std::atomic_int destroyedCount{0};
		for (int i = 0; i < 50; ++i)
		{
			QEventThread<int> worker = THREAD_SETUP_CAPTURE([&], {
				new DtorProbe(nullptr, &destroyedCount, eventLoop);
				ON_DATA_RECEIVED_CAPTURE([&], { (void) data; });
			});
			worker.enqueue(1);
			worker.enqueue(2);
		}
		EXPECT_EQ(destroyedCount.load(), 50);
	}

	TEST_F(QEventThreadFixture, EnqueueDuringConcurrentDestructionDoesNotCrash)
	{
		for (int iter = 0; iter < 20; ++iter)
		{
			auto        worker = std::make_shared<QEventThread<int>>(
			    THREAD_SETUP_CAPTURE([&], { ON_DATA_RECEIVED_CAPTURE([&], { (void) data; }); }));
			std::thread producer([worker] {
				for (int i = 0; i < 100; ++i) worker->enqueue(i);
			});
			worker.reset();    // destroy (join) while the producer is mid-flight
			producer.join();
		}
		SUCCEED();
	}

	// #413: the OUTPUT lane's teardown must be symmetric with the input lane — a drainOutput metacall queued to the
	// owner thread must NOT survive the QEventThread and fire against freed members. Register an onOutput handler,
	// make the worker EMIT so a drain is posted, wait until it IS queued, but DO NOT pump the owner loop (so the drain
	// stays undispatched), then destroy the worker. Before the fix the drain targeted QCoreApplication (app-lifetime)
	// and fired after destruction → use-after-free of the freed QEventThread + its handler's captures. With the fix the
	// drain targets a QObject the QEventThread owns, destroyed after the join, so the queued drain is purged. Run under
	// a per-iteration ASAN/normal build: a surviving drain would touch freed memory here.
	TEST_F(QEventThreadFixture, DestroyWithAQueuedOutputDrainDoesNotUseAfterFree)
	{
		for (int iter = 0; iter < 20; ++iter)
		{
			std::atomic_int delivered{0};
			bool            handlerAlive = true;
			{
				auto worker = std::make_unique<QEventThread<int, int>>(
				    THREAD_SETUP_CAPTURE([&], { ON_DATA_RECEIVED_CAPTURE([&], { EMIT(data + 1); }); }));
				// The handler captures a local (handlerAlive) by reference — a firing drain after teardown would read it.
				worker->onOutput([&](int& v) { (void) v; ++delivered; (void) handlerAlive; });
				worker->enqueue(7);
				// Wait until the worker has produced output AND the drain has been posted (output present, but we never
				// pump this thread's event loop, so drainOutput stays QUEUED, undispatched), then destroy the worker.
				EXPECT_TRUE(waitFor([&] { return worker->outputs().size() >= 1u || delivered.load() >= 1; }));
				worker.reset();    // ~QEventThread: join, then purge the owner-thread drain target (the queued drain dies)
			}
			handlerAlive = false;
			// Pump now: any drain that WRONGLY survived the destruction would fire here against freed state. With the
			// fix, nothing fires (the metacall was purged), so this is a clean spin.
			for (int i = 0; i < 50; ++i)
				QCoreApplication::processEvents();
		}
		SUCCEED();    // reaching here without a crash/ASAN report proves the queued drain did not outlive the worker
	}

	TEST_F(QEventThreadFixture, StopTokenIsObservableOnTheWorker)
	{
		std::atomic_bool everPolled{false};
		{
			QEventThread<> worker = THREAD_SETUP_CAPTURE([&], {
				auto* timer = new QTimer(eventLoop);
				QObject::connect(timer, &QTimer::timeout, eventLoop, [&everPolled, stop] {
					everPolled.store(true);
					(void) stop.stop_requested();    // the token is live + readable on the worker
				});
				timer->start(1);
			});
			EXPECT_TRUE(waitFor([&] { return everPolled.load(); }));
		}
		EXPECT_TRUE(everPolled.load());
	}

	//======================================================================================================================
	//      NO EXCEPTION ESCAPES — setup, data handler, runOnThread; std + non-std throws
	//======================================================================================================================

	// Pump the owner (this) event loop until a marshaled worker exception is re-thrown on THIS thread, catching it. This
	// proves the FULL contract — worker catch → marshal → main-thread rethrow — AND consumes the queued rethrow so it
	// never detonates in a later, unrelated test (a real cross-test hazard: the rethrow is a queued app event). Returns
	// the caught what() (empty if a non-std throw, "<none>" if nothing surfaced in time).
	inline std::string pumpUntilMarshaledThrow(int budgetMs = 2000)
	{
		for (int i = 0; i < budgetMs; ++i)
		{
			try
			{
				QCoreApplication::processEvents();
				if (std::exception_ptr captured = logerr::takeException())
					std::rethrow_exception(captured);
			}
			catch (const std::exception& e)
			{
				return e.what();
			}
			catch (...)
			{
				return "";    // non-std throw was marshaled + re-thrown here
			}
			QThread::msleep(1);
		}
		return "<none>";
	}

	TEST_F(QEventThreadFixture, OutputHandlerExceptionIsCapturedAndLaterOutputsStillRun)
	{
		std::vector<int> delivered;
		QEventThread<void, int> worker = THREAD_SETUP_CAPTURE([&], {
			EMIT(1);
			EMIT(2);
			EMIT(3);
		});

		worker.onOutput([&](int& value) {
			if (value == 1)
				throw std::runtime_error("boom in output handler");
			delivered.push_back(value);
		});

		EXPECT_EQ(delivered, (std::vector<int>{2, 3}));
		EXPECT_EQ(pumpUntilMarshaledThrow(), std::string("boom in output handler"));
	}

	TEST_F(QEventThreadFixture, GuardedSignalLambdaSurfacesErrOnMainThreadAndWorkerSurvives)
	{
		std::atomic_bool signalRan{false};
		std::atomic_bool setupContinued{false};
		std::atomic_bool laterWorkRan{false};
		std::string      receivedName;

		QEventThread<> worker = THREAD_SETUP_CAPTURE([&], {
			auto* source = new QObject(eventLoop);
			const auto connection = connectGuarded(source, &QObject::objectNameChanged, [&](const QString& name) {
				receivedName = name.toStdString();
				signalRan.store(true);
				ERR("boom in guarded signal lambda");
			});
			if (!connection)
				throw std::runtime_error("guarded connection failed");
			source->setObjectName(QStringLiteral("guarded-source"));
			setupContinued.store(true);    // the ERR was caught inside the signal wrapper, so setup continues normally
		});

		EXPECT_TRUE(signalRan.load());
		EXPECT_TRUE(setupContinued.load());
		EXPECT_EQ(receivedName, "guarded-source");
		worker.runOnThread([&] { laterWorkRan.store(true); });
		EXPECT_TRUE(waitFor([&] { return laterWorkRan.load(); }));
		EXPECT_NE(pumpUntilMarshaledThrow().find("boom in guarded signal lambda"), std::string::npos);
		EXPECT_TRUE(worker.isRunning());
	}

	TEST_F(QEventThreadFixture, GuardedSignalFromOwnerIsQueuedToWorkerAndMayIgnoreArguments)
	{
		QObject                 ownerObject;
		std::atomic<QThread*>   callbackThread{nullptr};
		const QThread* const    ownerThread = QThread::currentThread();

		QEventThread<> worker = THREAD_SETUP_CAPTURE([&], {
			connectGuarded(&ownerObject, &QObject::objectNameChanged, [&] {
				callbackThread.store(QThread::currentThread());
			});
		});

		ownerObject.setObjectName(QStringLiteral("cross-thread-signal"));
		EXPECT_TRUE(waitFor([&] { return callbackThread.load() != nullptr; }));
		EXPECT_NE(callbackThread.load(), ownerThread);
	}

	TEST_F(QEventThreadFixture, AnExceptionInSetupIsCapturedAndReThrownOnTheMainThread)
	{
		{
			QEventThread<> worker = THREAD_SETUP_CAPTURE([&], { throw std::runtime_error("boom in setup"); });
			// The worker did NOT terminate the process; the fault is marshaled to us and re-thrown here on pump.
			EXPECT_EQ(pumpUntilMarshaledThrow(), std::string("boom in setup"));
		}
	}

	TEST_F(QEventThreadFixture, AnExceptionInTheDataHandlerIsCapturedAndTheWorkerSurvives)
	{
		std::atomic_bool afterRan{false};
		{
			QEventThread<int> worker = THREAD_SETUP_CAPTURE([&], {
				ON_DATA_RECEIVED_CAPTURE([&], {
					if (data == 0) throw std::runtime_error("boom in handler");
					afterRan.store(true);
				});
			});
			worker.enqueue(0);    // throws inside the handler — must be contained
			worker.enqueue(1);    // must still be delivered (the loop kept turning)
			EXPECT_TRUE(waitFor([&] { return afterRan.load(); }));
			EXPECT_EQ(pumpUntilMarshaledThrow(), std::string("boom in handler"));    // consume the marshaled fault
		}
		EXPECT_TRUE(afterRan.load());
	}

	TEST_F(QEventThreadFixture, AnExceptionInRunOnThreadIsCapturedAndTheWorkerSurvives)
	{
		std::atomic_bool afterRan{false};
		{
			QEventThread<> worker = THREAD_SETUP_CAPTURE([&], {});
			worker.runOnThread([] { throw std::runtime_error("boom in runOnThread"); });
			worker.runOnThread([&] { afterRan.store(true); });
			EXPECT_TRUE(waitFor([&] { return afterRan.load(); }));
			EXPECT_EQ(pumpUntilMarshaledThrow(), std::string("boom in runOnThread"));
		}
		EXPECT_TRUE(afterRan.load());
	}

	TEST_F(QEventThreadFixture, ANonStdExceptionIsAlsoCaught)
	{
		std::atomic_bool afterRan{false};
		{
			QEventThread<> worker = THREAD_SETUP_CAPTURE([&], {});
			worker.runOnThread([] { throw 42; });
			worker.runOnThread([&] { afterRan.store(true); });
			EXPECT_TRUE(waitFor([&] { return afterRan.load(); }));
			EXPECT_EQ(pumpUntilMarshaledThrow(), std::string(""));    // non-std throw surfaced (caught by catch(...))
		}
		EXPECT_TRUE(afterRan.load());
	}

	//======================================================================================================================
	//      CONCURRENCY — many independent workers, no cross-talk
	//======================================================================================================================

	TEST_F(QEventThreadFixture, ManyDataWorkersCoexistIndependently)
	{
		constexpr int n = 12;
		std::vector<std::unique_ptr<QEventThread<int>>> workers;
		std::vector<std::atomic_int>                    counts(n);
		for (int i = 0; i < n; ++i) counts[i].store(0);
		for (int i = 0; i < n; ++i)
		{
			std::atomic_int* c = &counts[i];
			workers.emplace_back(std::make_unique<QEventThread<int>>(
			    THREAD_SETUP_CAPTURE([c], { ON_DATA_RECEIVED_CAPTURE([c], { (void) data; c->fetch_add(1, std::memory_order_relaxed); }); })));
		}
		for (int i = 0; i < n; ++i)
			for (int k = 0; k < 10; ++k) workers[i]->enqueue(k);
		EXPECT_TRUE(waitFor([&] {
			for (int i = 0; i < n; ++i) if (counts[i].load() != 10) return false;
			return true;
		}, 5000));
		for (int i = 0; i < n; ++i) EXPECT_EQ(counts[i].load(), 10) << "worker " << i << " cross-talk";
		workers.clear();
		SUCCEED();
	}

	//======================================================================================================================
	//      MACRO call site — a composer struct that declares its worker with the [this] macros (member-primitive use)
	//======================================================================================================================

	struct MacroComposer
	{
		std::atomic<QThread*> bornOn{nullptr};
		std::atomic_int       consumed{0};

		QEventThread<int, int> thread = THREAD_SETUP({
			auto* t = new QTimer(eventLoop);    // `eventLoop` provided by the macro
			bornOn.store(t->thread());
			ON_DATA_RECEIVED({             // `data` provided by the macro; `this`/`input`/`output` captured
				consumed.fetch_add(1, std::memory_order_relaxed);
				EMIT(data * 2);            // EMIT sugar over the injected `output`
			});
			(void) stop;                   // `stop` provided by the macro
		});

		void feed(int v) { thread.enqueue(v); }
	};

	TEST_F(QEventThreadFixture, MacroCallSiteBuildsRunsConsumesAndEmitsOnTheWorker)
	{
		MacroComposer composer;
		EXPECT_NE(composer.bornOn.load(), nullptr);
		EXPECT_NE(composer.bornOn.load(), QThread::currentThread());
		composer.feed(3);
		composer.feed(4);
		EXPECT_TRUE(waitFor([&] { return composer.consumed.load() == 2; }));
		EXPECT_TRUE(waitFor([&] { return composer.thread.outputs().size() == 2u; }));
		std::vector<int> results;
		int              v;
		while (composer.thread.outputs().try_pop(v)) results.push_back(v);
		ASSERT_EQ(static_cast<int>(results.size()), 2);
		EXPECT_EQ(results[0], 6);
		EXPECT_EQ(results[1], 8);
	}
	//======================================================================================================================
	//      DETERMINISTIC RACE-SURFACE COVERAGE — no processEvents polling, no timing hope: a gate makes ordering exact.
	//      These are the branches Helen used to chase as flakes; here they are pinned deterministically.
	//======================================================================================================================

	TEST_F(QEventThreadFixture, CoalescedWakeWhileTheWorkerIsBusyDeliversEveryItemOnce)
	{
		// Pin the coalesce branch deterministically: the FIRST item's handler BLOCKS on a gate, so the drain cannot
		// re-run; every further enqueue while blocked hits wake()'s "drain already pending" coalesce-return. Release the
		// gate → the single in-flight drain consumes the whole burst in order. No timing hope.
		std::binary_semaphore gate{0};
		std::atomic_bool      firstEntered{false};
		std::vector<int>      seen;
		std::atomic_int       done{0};
		{
			QEventThread<int> worker = THREAD_SETUP_CAPTURE([&], {
				ON_DATA_RECEIVED_CAPTURE([&], {
					if (data == 0) { firstEntered.store(true); gate.acquire(); }    // hold the worker inside the drain
					seen.push_back(data);
					done.fetch_add(1, std::memory_order_relaxed);
				});
			});
			worker.enqueue(0);
			EXPECT_TRUE(waitFor([&] { return firstEntered.load(); }));    // worker is now parked inside the handler
			for (int i = 1; i < 100; ++i) worker.enqueue(i);             // these all coalesce onto the pending drain
			gate.release();
			EXPECT_TRUE(waitFor([&] { return done.load() == 100; }, 5000));
		}
		ASSERT_EQ(static_cast<int>(seen.size()), 100);
		for (int i = 0; i < 100; ++i) EXPECT_EQ(seen[i], i) << "coalesced burst out of order at " << i;
	}

	TEST_F(QEventThreadFixture, EnqueueAfterStopHitsTheStoppedWakePathCleanly)
	{
		// Pin wake()'s stopped-return (m_loop cleared): stop + join, THEN enqueue. The wake finds no live loop and drops
		// the item without crashing. Deterministic — the dtor join guarantees the worker has fully unwound.
		std::atomic_int consumed{0};
		auto worker = std::make_unique<QEventThread<int>>(
		    THREAD_SETUP_CAPTURE([&], { ON_DATA_RECEIVED_CAPTURE([&], { (void) data; consumed.fetch_add(1); }); }));
		worker->requestStop();
		QThread::msleep(30);        // let the loop quit + clear m_loop
		worker->enqueue(1);         // wake() sees no loop → stopped-return, no crash
		worker->enqueue(2);
		worker.reset();             // join
		EXPECT_EQ(consumed.load(), 0);
	}

	TEST_F(QEventThreadFixture, CoalescedOutputNotificationWhileTheOwnerHasNotDrained)
	{
		// Pin notifyOutput's coalesce-return: with onOutput registered but the owner loop NOT pumped, the worker EMITs a
		// burst. The first EMIT posts a drain (m_outputDrainPending=true); every EMIT before the owner drains coalesces
		// (notifyOutput returns early). Then pump once → the single drain delivers the whole burst in order.
		std::binary_semaphore gate{0};
		std::atomic_bool      parked{false};
		std::vector<int>      got;
		{
			QEventThread<int, int> worker = THREAD_SETUP_CAPTURE([&], {
				ON_DATA_RECEIVED_CAPTURE([&], {
					// Emit a burst from a single input, all before the owner (this test thread) pumps its loop.
					parked.store(true);
					gate.acquire();                       // hold until the test says go, so all EMITs precede any drain
					for (int k = 0; k < 50; ++k) EMIT(data + k);
				});
			});
			worker.onOutput([&](int& v) { got.push_back(v); });
			worker.enqueue(100);
			EXPECT_TRUE(waitFor([&] { return parked.load(); }));
			gate.release();                                // worker now EMITs 50 items with no owner drain in between
			EXPECT_TRUE(waitFor([&] { return worker.outputs().size() == 50u; }, 5000));    // all queued, coalesced notify
			for (int i = 0; i < 500 && got.size() < 50; ++i) QCoreApplication::processEvents();
		}
		ASSERT_EQ(static_cast<int>(got.size()), 50);
		for (int i = 0; i < 50; ++i) EXPECT_EQ(got[i], 100 + i);
	}

	TEST_F(QEventThreadFixture, OnOutputHandlerWorksForAMoveOnlyOutputType)
	{
		// Exercise onOutput/drainOutput on a NON-<int,int> output instantiation (a move-only reply).
		std::vector<int> got;
		{
			QEventThread<int, std::unique_ptr<int>> worker = THREAD_SETUP_CAPTURE([&], {
				ON_DATA_RECEIVED_CAPTURE([&], { EMIT(std::make_unique<int>(data * 3)); });
			});
			worker.onOutput([&](std::unique_ptr<int>& v) { if (v) got.push_back(*v); });
			for (int i = 0; i < 4; ++i) worker.enqueue(i);
			EXPECT_TRUE(waitFor([&] { return worker.outputs().size() + static_cast<size_t>(got.size()) >= 4; }));
			for (int i = 0; i < 300 && got.size() < 4; ++i) QCoreApplication::processEvents();
		}
		ASSERT_EQ(static_cast<int>(got.size()), 4);
		for (int i = 0; i < 4; ++i) EXPECT_EQ(got[i], i * 3);
	}

	//======================================================================================================================
	//      USE CASE #5 — two QEventThreads wired as each other's producer/consumer (the pipe topology)
	//======================================================================================================================

	TEST_F(QEventThreadFixture, TwoWorkersPipedProducerToConsumerFlowEventDriven)
	{
		// A's output feeds B's input, purely event-driven: A emits, its output.onPush hands each item to B.enqueue, which
		// wakes B's own loop. No blocking, no CV — two loops, callback hand-off. (The encoder→sender pattern.)
		std::vector<int> bSaw;
		std::atomic_int  bDone{0};

		QEventThread<int, int> stageA = THREAD_SETUP_CAPTURE([&], {
			ON_DATA_RECEIVED_CAPTURE([&], { EMIT(data + 1); });    // A: x -> x+1
		});
		QEventThread<int> stageB = THREAD_SETUP_CAPTURE([&], {
			ON_DATA_RECEIVED_CAPTURE([&], { bSaw.push_back(data); bDone.fetch_add(1, std::memory_order_relaxed); });
		});
		// Wire A.output -> B.input via the reactive-queue's push callback (hand-off, no owner thread involved).
		stageA.outputs().onPush([&] { int v; while (stageA.outputs().try_pop(v)) stageB.enqueue(v); });

		for (int i = 0; i < 25; ++i) stageA.enqueue(i);
		EXPECT_TRUE(waitFor([&] { return bDone.load() == 25; }, 5000));
		ASSERT_EQ(static_cast<int>(bSaw.size()), 25);
		for (int i = 0; i < 25; ++i) EXPECT_EQ(bSaw[i], i + 1) << "pipe reorder at " << i;
	}

	//======================================================================================================================
	//      USE CASE — a move-only byte-frame message shape (the real networking payload), full-duplex
	//======================================================================================================================

	TEST_F(QEventThreadFixture, ByteFrameRequestReplyRoundTrips)
	{
		using Frame = std::vector<std::byte>;
		auto frame  = [](std::initializer_list<int> bytes) {
            Frame f;
            for (int b : bytes) f.push_back(static_cast<std::byte>(b));
            return f;
		};
		{
			QEventThread<Frame, Frame> codec = THREAD_SETUP_CAPTURE([&], {
				ON_DATA_RECEIVED_CAPTURE([&], {
					Frame reply = data;                       // echo + append a status byte (a stand-in for "process")
					reply.push_back(static_cast<std::byte>(0xFF));
					EMIT(std::move(reply));
				});
			});
			codec.enqueue(frame({1, 2, 3}));
			codec.enqueue(frame({9}));
			EXPECT_TRUE(waitFor([&] { return codec.outputs().size() == 2u; }));
			Frame r1, r2;
			ASSERT_TRUE(codec.outputs().try_pop(r1));
			ASSERT_TRUE(codec.outputs().try_pop(r2));
			ASSERT_EQ(r1.size(), 4u);
			EXPECT_EQ(r1.back(), static_cast<std::byte>(0xFF));
			ASSERT_EQ(r2.size(), 2u);
		}
	}

	//======================================================================================================================
	//      USE CASE — the PIMPL composer exemplar: a public class owning a worker that reacts to its OWN timer, re-emits
	//======================================================================================================================

	struct SelfDrivingPimpl
	{
		std::atomic_int  ticks{0};
		std::atomic_bool sawStopToken{false};

		// A self-driving worker: owns a timer (born on the worker), reacts to its own timeout. No input/output.
		QEventThread<> worker = THREAD_SETUP({
			auto* timer = new QTimer(eventLoop);
			QObject::connect(timer, &QTimer::timeout, eventLoop, [this, stop] {
				ticks.fetch_add(1, std::memory_order_relaxed);
				if (stop.stop_requested()) sawStopToken.store(true);
			});
			timer->start(1);
		});
	};

	TEST_F(QEventThreadFixture, PimplComposerSelfDrivesFromItsOwnTimer)
	{
		SelfDrivingPimpl p;
		EXPECT_TRUE(waitFor([&] { return p.ticks.load() >= 5; }));
		EXPECT_GE(p.ticks.load(), 5);    // the worker drove itself with no external enqueue
	}

	struct NonDefault
	{
		explicit NonDefault(int value) : value(value) {}
		NonDefault() = delete;
		int value;
	};

	TEST_F(QNotifyingQueueFixture, DrainSupportsNonDefaultConstructibleElements)
	{
		QNotifyingQueue<NonDefault> queue;
		int seen = 0;
		queue.onDataReceived([&](NonDefault& item) { seen = item.value; });
		queue.emplace(17);
		queue.drain([](auto&& work) { work(); });
		EXPECT_EQ(seen, 17);
	}

	TEST_F(QEventThreadFixture, NonDefaultConstructibleInputAndOutputRoundTrip)
	{
		int seen = 0;
		QEventThread<NonDefault, NonDefault> worker = THREAD_SETUP_CAPTURE([&], {
			ON_DATA_RECEIVED_CAPTURE([&], { output.emplace(data.value * 2); });
		});
		worker.onOutput([&](NonDefault& item) { seen = item.value; });
		worker.enqueue(NonDefault{21});
		for (int i = 0; i < 2000 && seen != 42; ++i)
		{
			QCoreApplication::processEvents();
			QThread::msleep(1);
		}
		EXPECT_EQ(seen, 42);
	}

	TEST_F(QEventThreadFixture, IsRunningBecomesFalseAfterWorkerActuallyStops)
	{
		QEventThread<> worker = THREAD_SETUP_CAPTURE([&], {});
		EXPECT_TRUE(worker.isRunning());
		worker.requestStop();
		EXPECT_TRUE(waitFor([&] { return !worker.isRunning(); }));
	}
}    // namespace test_eventThread
