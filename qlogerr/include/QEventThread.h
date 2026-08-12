#ifndef QEVENTTHREAD_H
#define QEVENTTHREAD_H

//----------------------------
//  INCLUDES
//----------------------------

#include <QCoreApplication>
#include <QEventLoop>
#include <QMetaObject>
#include <QObject>
#include <QThread>
#include <QTimer>

#include <logerr>
#include <concurrent_queue.h>

#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <mutex>
#include <stop_token>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

//----------------------------------------------------------------------------------------------------------------------
//      CLASS: QNotifyingQueue
//----------------------------------------------------------------------------------------------------------------------
/// @brief      A thread-safe FIFO that NOTIFIES on every push over TWO independent channels — a `onPush` callback AND
///             the underlying condition variable — so a consumer reacts however it likes and NEVER polls. The generic
///             reactive-queue primitive.
/// @tparam     T  the element type.
/// @details    Wraps a `concurrent_queue<T>`. A single `push`/`emplace` does both, without ever blocking the producer:
///             - fires the owner-supplied **`onPush` callback** (the async channel — QEventThread posts a coalesced
///               drain onto the owner's Qt thread through it), and
///             - wakes the concurrent_queue's **condition variable**, so a plain (non-Qt) consumer thread with no event
///               loop can BLOCK on `wait_pop_for()` instead. (The producer's `notify` is non-blocking either way.)
///             It also carries an optional per-item CONSUMER (`onDataReceived`) that `drain()` invokes FIFO — this is
///             how QEventThread turns its INPUT queue into a handler-driven worker without any handle back to itself.
///             Standalone (only `concurrent_queue` + std); reusable anywhere a "tell me when something arrives" queue
///             is wanted. The `onPush` callback runs on the PUSHING thread — keep it cheap (post/notify, no work).
//----------------------------------------------------------------------------------------------------------------------
template<typename T>
class QNotifyingQueue
{
public:
	using value_type    = T;                         ///< the element type (std-container convention)
	using NotifyHandler = std::function<void()>;     ///< the async push callback signature
	using ItemHandler   = std::function<void(T&)>;   ///< the per-item drain-consumer signature

	//------------------------------
	//	CONSTRUCTORS
	//------------------------------
	QNotifyingQueue()                                  = default;
	QNotifyingQueue(const QNotifyingQueue&)            = delete;    ///< non-copyable (it holds live callbacks/consumers)
	QNotifyingQueue& operator=(const QNotifyingQueue&) = delete;

	//------------------------------
	//	NOTIFICATION CHANNELS
	//------------------------------
	/// @brief      Set the callback fired after each push — the async "something arrived" signal. Replaces any previous.
	/// @param[in]  handler  a nullary callback run on the PUSHING thread; keep it cheap (post/notify, no work).
	void onPush(NotifyHandler handler) { m_onPush = std::move(handler); }

	/// @brief      Set the per-item consumer that `drain()` invokes for each dequeued item, FIFO. Replaces any previous.
	/// @param[in]  handler  `void(T&)`, invoked once per item by `drain()`.
	void onDataReceived(ItemHandler handler) { m_onItem = std::move(handler); }

	//------------------------------
	//	ENQUEUE (wakes both channels)
	//------------------------------
	/// @brief      Enqueue by copy, then wake both channels (the onPush callback + the CV).
	/// @param[in]  value  the item to enqueue.
	void push(const T& value)
	{
		m_queue.push(value);
		fire();
	}

	/// @brief      Enqueue by move, then wake both channels (the onPush callback + the CV).
	/// @param[in]  value  the item to enqueue (moved in).
	void push(T&& value)
	{
		m_queue.push(std::move(value));
		fire();
	}

	/// @brief      Construct an element in place at the tail, then wake both channels.
	/// @tparam     Args  the element's constructor argument types.
	/// @param[in]  args  arguments forwarded to the element's constructor.
	template<typename... Args>
	void emplace(Args&&... args)
	{
		m_queue.emplace(std::forward<Args>(args)...);
		fire();
	}

	//------------------------------
	//	DEQUEUE
	//------------------------------
	/// @brief      Pop the front into @p destination without blocking.
	/// @param[out] destination  receives the dequeued item on success.
	/// @return     true if an item was dequeued, false if the queue was empty.
	bool try_pop(T& destination) { return m_queue.try_pop(destination); }
	[[nodiscard]] std::optional<T> try_pop() { return m_queue.try_pop(); }

	/// @brief      Block up to @p timeout for an item, popping it into @p destination — the CV channel, for a consumer
	///             with no event loop (a bare thread). QEventThread's own worker never uses this (it reacts via its loop).
	/// @tparam     Rep     the timeout duration representation.
	/// @tparam     Period  the timeout duration period.
	/// @param[out] destination  receives the dequeued item on success.
	/// @param[in]  timeout      the maximum time to wait.
	/// @return     true if an item arrived within @p timeout, false on timeout.
	template<class Rep, class Period>
	bool wait_pop_for(T& destination, const std::chrono::duration<Rep, Period>& timeout)
	{
		return m_queue.try_pop_for(destination, timeout);
	}

	/// @brief      Pop every currently-queued item and hand each to the registered `onDataReceived` consumer, FIFO,
	///             applying @p wrap around each invocation. A no-op if no consumer is set.
	/// @tparam     Wrap  an invocable taking a nullary callable and running it (QEventThread passes its exception-guard).
	/// @param[in]  wrap  applied around each per-item consumer call.
	template<typename Wrap>
	void drain(Wrap&& wrap)
	{
		if (!m_onItem)
			return;
		while (auto item = m_queue.try_pop())
		{
			T& ref = *item;
			wrap([this, &ref] { m_onItem(ref); });
		}
	}

	//------------------------------
	//	OBSERVERS
	//------------------------------
	[[nodiscard]] bool   empty() const { return m_queue.empty(); }    ///< whether the queue was empty at the instant checked
	[[nodiscard]] size_t size() const { return m_queue.size(); }      ///< the element count at the instant checked
	void                 clear() { m_queue.clear(); }                 ///< drop all queued elements (does not notify)

private:
	/// @brief   Fire the async push callback if one is registered (the CV was already woken by the underlying queue).
	void fire()
	{
		if (m_onPush)
			m_onPush();
	}

	concurrent_queue<T> m_queue;
	NotifyHandler       m_onPush;    ///< async channel: fired on the pushing thread after each enqueue
	ItemHandler         m_onItem;    ///< per-item consumer invoked by drain() (used for the INPUT lane)
};

//----------------------------------------------------------------------------------------------------------------------
//      CLASS: QEventThread
//----------------------------------------------------------------------------------------------------------------------
/// @brief      A background thread that runs its OWN private `QEventLoop`, for a component to COMPOSE (hold as a member),
///             never to derive from. Optionally owns a thread-safe INPUT queue producers feed it and a NOTIFYING OUTPUT
///             queue it feeds back — the full-duplex reactive-worker pattern, boilerplate-free.
/// @tparam     In   the type producers `enqueue()` for the worker to consume. `void` (default) ⇒ no input queue.
/// @tparam     Out  the type the worker pushes to `output` for the owner to consume. `void` (default) ⇒ no output queue.
/// @details    Packages the one-true worker pattern used across lib/transport, lib/hawser, and QNativeProcess so it is
///             written once, correctly, instead of hand-rolled (and subtly mis-parented) in each. F1-grade: no busy
///             waits, no avoidable blocks — the startup handshake is a C++20 atomic wait (futex), wakes are coalesced
///             (one drain per burst), drains are non-blocking `try_pop`, and the ONLY block is the dtor join barrier.
///
///             THE SETUP BODY (via THREAD_SETUP) receives five injected names — the whole generic reactive surface, and
///             NO handle back to the thread object:
///             - `eventLoop` — the worker's `QEventLoop*`. Parent every object you create to it (`new T(eventLoop)`); the
///               loop reaps them on the worker at unwind (RAII).
///             - `stop`   — a `std::stop_token`, for a bounded graceful teardown (poll it from a timer, etc.).
///             - `input`  — the worker handle. With In != void, register its consumer via `ON_DATA_RECEIVED`.
///             - `output` — a `QNotifyingQueue<Out>&` (present when Out != void). Push to it from ANY worker-side event
///               — a data handler, a socket `readyRead`, a timer — and EACH PUSH notifies the owner to drain. The
///               emission trigger is the enqueue itself: 0, 1, or N outputs per input, from anywhere on the worker. Use
///               `EMIT(x)` as terse sugar for `output.push(x)`.
///             - `connectGuarded` — connect a Qt signal to a lambda under the worker exception boundary. An `ERR` or
///               other throw in that lambda is captured and surfaced by the application thread rather than terminating.
///
///             DIRECTION MODEL (each side opt-in via its template arg, compiled out with `if constexpr` otherwise):
///             - **Input:** `enqueue(in)` on any thread → the worker's handler / drain loop.
///             - **Output:** any worker push to `output` → the owner's onOutput handler (on the owner's thread) or the
///               owner draining `outputs()` itself.
///             - A data-less `QEventThread<>` is a pure self-driving worker (own a socket/timer, react to its own
///               events) with an occasional `runOnThread()` one-off callable.
///
///             Guarantees:
///             - **Objects are born on the worker** (parented to `eventLoop`), reaped on the worker at unwind. No
///               `moveToThread`, no `QThread` — a raw `std::jthread` runs the loop.
///             - **Startup handshake.** The constructor BLOCKS on a C++20 atomic wait (futex-backed — construction-time
///               only) until the loop is live and setup has run, so a call right after construction is never dropped.
///             - **stop_token is the sole exit SSOT.** A `std::stop_callback` quits the loop SYNCHRONOUSLY on stop
///               (covering a stop requested before the loop was live). The dtor requests stop and JOINS as a barrier.
///             - **No C++ exception escapes the worker thread; a fault is LOGGED and GRACEFULLY RE-THROWN ON THE MAIN
///               THREAD.** The setup body, every input handler, and every `runOnThread()` run under `try/catch (...)`, so
///               an escaping C++ exception NEVER unwinds out of the worker (which would `std::terminate` the whole
///               process from a background thread). Instead it is LOGGED via logerr at the worker (recorded even if the
///               process is torn down before delivery), then published through logerr's synchronized exception slot.
///               A queued no-op wakes the application object's thread; logerr's application `notify()` override consumes
///               and rethrows the failure only after Qt's base dispatcher returns. With no application, the failure
///               remains available to `LOGERR_RETHROW()`. Caveat: this covers C++ exceptions only — a structured/hardware
///               fault (SEH access violation) is not caught by `catch (...)` under `/EHsc`. A raw
///               `QObject::connect(sender, signal, eventLoop, lambda)` remains unguarded and is appropriate only when
///               the lambda cannot throw; use `connectGuarded(sender, signal, lambda)` otherwise.
///
///             PLATINUM CALL SITE — a full-duplex reactive worker:
///             @code
///             class LinkPrivate : public QObject {
///                 QSslSocket* m_sock = nullptr;                          // members the worker touches — before the thread
///                 QEventThread<Request, Reply> m_thread = THREAD_SETUP({
///                     m_sock = new QSslSocket(eventLoop);                // born on the worker
///                     ON_DATA_RECEIVED({ EMIT(process(data)); });        // Request in (as `data`) → Reply out
///                 });                                                    // declared LAST → joined FIRST
///             public:
///                 void send(Request r) { m_thread.enqueue(std::move(r)); }
///                 // owner consumes replies via m_thread.onOutput(...) or outputs().
///             };
///             @endcode
//----------------------------------------------------------------------------------------------------------------------
template<typename In = void, typename Out = void>
class QEventThread
{
	static constexpr bool HasInput  = !std::is_void_v<In>;
	static constexpr bool HasOutput = !std::is_void_v<Out>;
	// A `void` side must still name a concrete element type wherever the class is unconditionally instantiated (queue
	// members, handler aliases): `concurrent_queue<void>` / `std::function<void(void&)>` are ill-formed. Map void to a
	// harmless placeholder for those internals; the PUBLIC API keeps the real In/Out (SFINAE'd out when void).
	using InElement  = std::conditional_t<HasInput, In, std::monostate>;
	using OutElement = std::conditional_t<HasOutput, Out, std::monostate>;
	// ONE ordered input lane. enqueue(In) and runOnThread(callable) push into the SAME FIFO as a variant, so they drain
	// in true SUBMISSION order — no cross-lane inversion (a runOnThread before an enqueue runs before it, always). The
	// alternatives: index 0 = a typed input datum; index 1 = a one-off callable.
	using Submission = std::variant<InElement, std::function<void()>>;

public:
	using OutputQueue   = QNotifyingQueue<OutElement>;        ///< the `output` ref injected into the setup body (Out != void)
	using InputHandler  = std::function<void(InElement&)>;    ///< per-input handler, registered via ON_DATA_RECEIVED
	using OutputHandler = std::function<void(OutElement&)>;   ///< per-output handler, registered via onOutput

	/// Whether the constructor waits for the worker's loop to come live before returning.
	enum class StartMode
	{
		/// The ctor BLOCKS (futex atomic-wait) until the loop is live and setup has run — the default. A call made right
		/// after construction is serviced against a guaranteed-live loop. Use everywhere unless per-object construction
		/// cost is proven to matter.
		Blocking,
		/// The ctor RETURNS AT ONCE — it does NOT wait for the loop. A pre-loop `enqueue()`/`runOnThread()` is still safe:
		/// it is BUFFERED in the submission lane and drained the instant the loop comes live (setup runs, then drainInput
		/// consumes the backlog in submission order). For the perf-critical case where MANY workers are constructed in a
		/// burst (e.g. one per ship at Session Start) and a per-object blocking handshake would serialize into a stall
		/// (#191/#207): the caller never blocks, yet no submitted work is ever dropped or reordered.
		NonBlocking
	};

private:
	/// Invoke a Qt slot with the longest signal-argument prefix it accepts. Qt permits slots to omit trailing signal
	/// arguments (including QPrivateSignal); the generic guard wrapper must preserve that normal connect behavior.
	template<size_t Count, typename Slot, typename Tuple>
	static void invokeCompatible(Slot& slot, Tuple&& arguments)
	{
		[&]<size_t... Indices>(std::index_sequence<Indices...>) {
			if constexpr (std::is_invocable_v<
			                  Slot&, decltype(std::get<Indices>(std::forward<Tuple>(arguments)))...>)
			{
				std::invoke(slot, std::get<Indices>(std::forward<Tuple>(arguments))...);
			}
			else if constexpr (Count > 0)
			{
				invokeCompatible<Count - 1>(slot, std::forward<Tuple>(arguments));
			}
			else
			{
				static_assert(std::is_invocable_v<Slot&>, "guarded slot is incompatible with the signal arguments");
			}
		}(std::make_index_sequence<Count>{});
	}

	//------------------------------------------------------------------------------------------------------------------
	//      CLASS: WorkerInput  (the `input` handle injected into the setup body)
	//------------------------------------------------------------------------------------------------------------------
	/// @brief   The setup body's worker handle: register the per-datum handler (ON_DATA_RECEIVED) or connect a Qt signal
	///          to an exception-guarded lambda. Holds no queue of its own — it forwards to the owning QEventThread.
	class WorkerInput
	{
	public:
		explicit WorkerInput(QEventThread* owner) : m_owner(owner) {}
		/// Register the per-input handler (prefer the ON_DATA_RECEIVED macro). Runs once per enqueued datum, FIFO, guarded.
		void onDataReceived(InputHandler handler) { m_owner->onDataReceived(std::move(handler)); }

		/// Connect @p signal to @p slot on the worker's event loop, guarding the slot so an ERR/throw is captured and
		/// surfaced on the application thread instead of escaping QEventLoop::exec() and terminating the process.
		template<typename Sender, typename Signal, typename Slot>
		QMetaObject::Connection connectGuarded(Sender* sender, Signal signal, Slot&& slot)
		{
			Q_ASSERT(m_owner->m_loop);
			Q_ASSERT(QThread::currentThread() == m_owner->m_loop->thread());
			return QObject::connect(
			    sender, signal, m_owner->m_loop,
			    [owner = m_owner, slot = std::forward<Slot>(slot)](auto&&... args) mutable noexcept {
				    owner->runGuarded([&] {
					    auto arguments = std::forward_as_tuple(std::forward<decltype(args)>(args)...);
					    owner->template invokeCompatible<sizeof...(args)>(slot, std::move(arguments));
				    });
			    });
		}
	private:
		QEventThread* m_owner;
	};

	using SetupFunctionStore = std::function<void(QEventLoop*, std::stop_token, WorkerInput&, OutputQueue&)>;

public:

	//===========================================================================================================
	//      CONSTRUCTORS
	//===========================================================================================================

	//------------------------------------------------------------------------------------------------------------------
	//      FUNCTION: QEventThread [public]
	//------------------------------------------------------------------------------------------------------------------
	/// @brief      Start the worker thread; by default BLOCK (atomic-wait handshake) until its loop is live and @p setup
	///             has run. Pass StartMode::NonBlocking to return at once (a pre-loop submission is buffered + drained
	///             when the loop comes live — for a construction-cost-sensitive burst; see StartMode).
	/// @tparam     Setup  any invocable callable as `void(QEventLoop* loop, std::stop_token stop, InputQueue& input,
	///                    OutputQueue& output)` — usually a THREAD_SETUP generic lambda.
	/// @param[in]  setup  the worker-body setup callable.
	/// @param[in]  mode   Blocking (default) waits for the loop; NonBlocking returns immediately.
	/// @details    Templated + NON-explicit so the member-initializer `QEventThread<...> m = THREAD_SETUP({…})` — whose
	///             macro yields a generic lambda — copy-inits in ONE user-defined conversion. SFINAE'd off the
	///             copy/move ctors so those still win for a same-type argument.
	//------------------------------------------------------------------------------------------------------------------
	template<typename Setup, std::enable_if_t<!std::is_same_v<std::decay_t<Setup>, QEventThread>, int> = 0>
	QEventThread(Setup&& setup, StartMode mode = StartMode::Blocking)
	    : m_setup(std::forward<Setup>(setup))
	{
		// The submission IS the trigger (SSOT): every push to the input lane wakes the worker; every push to `output`
		// wakes the owner. Wired once here so producers/handlers just push — no explicit wake at any call site.
		m_submissions.onPush([this] { wake(); });
		if constexpr (HasOutput)
		{
			// Born on the OWNER thread (this ctor runs there, where onOutput is registered and drainOutput must run), so
			// the output-drain metacall targets an object with owner-thread affinity AND this QEventThread's lifetime —
			// destroyed after the join in ~QEventThread, purging any queued drain (#413).
			m_outputDrainTarget = std::make_unique<QObject>();
			m_output.onPush([this] { notifyOutput(); });
		}

		m_thread = std::jthread(
		    [this](std::stop_token stopToken)
		    {
			    QEventLoop eventLoop;    // OWNS (parents) every object setup creates; reaps them on unwind
			    {
				    m_running.store(true, std::memory_order_release);
				    const std::lock_guard<std::mutex> guard(m_loopMutex);
				    m_loop = &eventLoop;    // published for wake(); under the mutex so a wake never sees a dying loop
			    }

			    // stop_token is the sole exit SSOT: quit the loop synchronously on stop (covers a pre-loop fast destroy),
			    // posted queued so it never touches the loop cross-thread directly.
			    std::stop_callback stopQuit(stopToken, [&eventLoop]
			                                { QMetaObject::invokeMethod(&eventLoop, [&eventLoop] { eventLoop.quit(); }, Qt::QueuedConnection); });

			    QTimer::singleShot(0, &eventLoop,
			                       [this, &eventLoop, stopToken]
			                       {
				                       // If a stop beat the loop coming up, still release the ctor's handshake (never hang)
				                       // and quit without running setup. In Blocking mode this is unreachable via the public
				                       // API (the ctor blocks on the handshake released below, so no caller holds a handle to
				                       // requestStop() before this runs); in NonBlocking mode the ctor returned immediately,
				                       // so a fast construct→requestStop()/destroy CAN land here first — this guard is what
				                       // makes that safe (the pre-loop-stop path proven by QEventThread.tla).
				                       if (stopToken.stop_requested()) [[unlikely]]
				                       {
					                       releaseHandshake();
					                       eventLoop.quit();
					                       return;
				                       }
				                       runGuarded([this, &eventLoop, stopToken] { m_setup(&eventLoop, stopToken, m_input, m_output); });
				                       // Setup has now created every worker-owned object a submitted closure may touch. Open the
				                       // drain gate ONLY here, so a wake()-posted drainInput that raced ahead of this singleShot
				                       // (in NonBlocking mode both are queued loop events) left the submissions untouched and this
				                       // is the FIRST drain — no owner closure can run before the state it relies on exists.
				                       m_setupComplete.store(true, std::memory_order_release);
				                       drainInput();    // consume anything submitted during/before setup
				                       releaseHandshake();
			                       });

			    eventLoop.exec();
			    {
				    const std::lock_guard<std::mutex> guard(m_loopMutex);
				    m_loop = nullptr;    // stop targeting the loop before its stack object dies; wake() becomes a no-op
			    }
				    m_submissions.clear();    // a stopped worker never drains again — release pending captures
				    m_running.store(false, std::memory_order_release);
			    });

		if (mode == StartMode::Blocking)
			m_ready.wait(false, std::memory_order_acquire);    // handshake: return only once the loop is live + setup ran
		// NonBlocking: return at once. A pre-loop enqueue/runOnThread is buffered in m_submissions (its onPush wake
		// no-ops while m_loop is null) and drained by the singleShot's drainInput() the instant the loop is live — so
		// no submission is dropped or reordered, yet the caller never blocks.
	}

	//------------------------------------------------------------------------------------------------------------------
	//      FUNCTION: ~QEventThread [public]
	//------------------------------------------------------------------------------------------------------------------
	/// @brief      Request stop and JOIN the worker as a barrier, then purge the output-drain target, so no worker
	///             callback and no queued output drain runs after a member it reads is destroyed (the UAF class this
	///             pattern exists to prevent).
	/// @details    The join covers the INPUT lane (the worker's loop dies with the thread). It runs on the owner thread,
	///             which is therefore NOT pumping events during the block — so no drain metacall fires mid-teardown.
	///             After the join the worker is dead (it can post no further drain), so destroying m_outputDrainTarget
	///             here purges any drain metacall ALREADY queued on the owner thread (Qt removes an object's posted
	///             events on destruction) — closing the OUTPUT lane symmetrically with the input lane (#413).
	///             This is a lifetime barrier, not a flush barrier: pending submissions may be discarded. A component
	///             requiring delivery must enqueue and wait for its own ordered barrier before destruction.
	//------------------------------------------------------------------------------------------------------------------
	~QEventThread()
	{
		Q_ASSERT(!m_outputDrainTarget || QThread::currentThread() == m_outputDrainTarget->thread());
		m_thread.request_stop();
		if (m_thread.joinable())
			m_thread.join();          // BARRIER (dtor-only): the sole tolerated block — fully stop before members die.
		m_outputDrainTarget.reset();  // AFTER the join (no more posts) — purges any drain still queued on the owner thread
	}

	QEventThread(const QEventThread&)            = delete;
	QEventThread& operator=(const QEventThread&) = delete;
	QEventThread(QEventThread&&)                 = delete;    // holds a running thread capturing `this`; non-movable
	QEventThread& operator=(QEventThread&&)      = delete;

	//===========================================================================================================
	//      INPUT — producers feed the worker (any thread)
	//===========================================================================================================

	//------------------------------------------------------------------------------------------------------------------
	//      FUNCTION: enqueue [public]  (input workers only, In != void)
	//------------------------------------------------------------------------------------------------------------------
	/// @brief      Hand an input item to the worker: push it on the thread-safe FIFO and wake the worker (coalesced).
	/// @param[in]  item  the datum to process on the worker (moved in). Thread-safe; FIFO; a no-op once stopped.
	//------------------------------------------------------------------------------------------------------------------
	template<typename U = In, std::enable_if_t<!std::is_void_v<U>, int> = 0>
	void enqueue(U item)
	{
		// index 0 = a typed datum, pushed into the ONE ordered lane; onPush wakes the worker (the enqueue is the trigger).
		m_submissions.emplace(std::in_place_index<0>, std::move(item));
	}

	//------------------------------------------------------------------------------------------------------------------
	//      FUNCTION: runOnThread [public]
	//------------------------------------------------------------------------------------------------------------------
	/// @brief      Run a one-off callable on the worker thread — the occasional "do this over there" without a signal or
	///             a data item. Thread-safe; a no-op once stopped. Ordered WITH enqueue(): a callable and a datum drain
	///             in true submission order (they share one lane), so a runOnThread before an enqueue runs before it.
	/// @tparam     Callable  any nullary invocable; stored as a std::function<void()> in the ordered submission lane.
	/// @param[in]  work      the callable to run on the worker (forwarded into the lane).
	//------------------------------------------------------------------------------------------------------------------
	template<typename Callable>
	void runOnThread(Callable&& work)
	{
		// index 1 = a one-off callable, in the SAME ordered lane as enqueue()'d data; onPush wakes the worker.
		m_submissions.emplace(std::in_place_index<1>, std::function<void()>(std::forward<Callable>(work)));
	}

	//===========================================================================================================
	//      OUTPUT — the worker feeds results back (pushes to `output`, which notifies)
	//===========================================================================================================

	//------------------------------------------------------------------------------------------------------------------
	//      FUNCTION: onOutput [public]  (output workers only; register from the OWNER thread, e.g. in the ctor body)
	//------------------------------------------------------------------------------------------------------------------
	/// @brief      Register the per-output handler, run once per pushed item, in FIFO order, on the owner thread that
	///             constructed this QEventThread. Call this from that same thread; debug builds enforce the contract.
	/// @param[in]  handler  `void(Out&)` — receives each result by reference on the owner's thread.
	/// @details    Registered on the owner side (not in setup): the owner is typically GUI-affinity and wants results
	///             there. If no handler is registered, the owner drains outputs() itself.
	//------------------------------------------------------------------------------------------------------------------
	template<typename U = Out, std::enable_if_t<!std::is_void_v<U>, int> = 0>
	void onOutput(OutputHandler handler)
	{
		Q_ASSERT(m_outputDrainTarget && QThread::currentThread() == m_outputDrainTarget->thread());
		m_outputHandler = std::move(handler);
		drainOutput();    // deliver anything already pushed before the handler was registered
	}

	//------------------------------------------------------------------------------------------------------------------
	//      FUNCTION: outputs [public]  (output workers only)
	//------------------------------------------------------------------------------------------------------------------
	/// @brief      The OUTPUT queue, for the owner to drain itself (e.g. `while (outputs().try_pop(r)) …`) instead of
	///             registering onOutput. Thread-safe.
	/// @return     a reference to the worker's output queue (a QNotifyingQueue<Out>).
	//------------------------------------------------------------------------------------------------------------------
	template<typename U = Out, std::enable_if_t<!std::is_void_v<U>, int> = 0>
	OutputQueue& outputs() { return m_output; }

	//===========================================================================================================
	//      WORKER SIDE — called from within setup
	//===========================================================================================================

	//------------------------------------------------------------------------------------------------------------------
	//      FUNCTION: onDataReceived [public]  (input workers only; prefer the ON_DATA_RECEIVED macro)
	//------------------------------------------------------------------------------------------------------------------
	/// @brief      Register the per-input handler, run once per enqueued item in FIFO order on the worker, guarded.
	/// @param[in]  handler  `void(In&)`, invoked once per enqueued datum on the worker (prefer the ON_DATA_RECEIVED macro).
	//------------------------------------------------------------------------------------------------------------------
	template<typename U = In, std::enable_if_t<!std::is_void_v<U>, int> = 0>
	void onDataReceived(InputHandler handler)
	{
		m_inputHandler = std::move(handler);    // drainInput dispatches each datum submission through this
		wake();                                 // consume anything submitted before the handler was registered
	}

	//===========================================================================================================
	//      CONTROL
	//===========================================================================================================

	/// Ask the worker to stop and quit its loop, without waiting. Idempotent; the dtor also does this and then joins.
	void requestStop() noexcept { m_thread.request_stop(); }

	/// Whether the worker function is currently alive. A non-blocking worker may briefly report false during startup.
	[[nodiscard]] bool isRunning() const noexcept { return m_running.load(std::memory_order_acquire); }

private:
	//----------------------------
	//  HELPERS
	//----------------------------

	void releaseHandshake() noexcept
	{
		m_ready.store(true, std::memory_order_release);
		m_ready.notify_all();
	}

	//------------------------------------------------------------------------------------------------------------------
	//      FUNCTION: wake [private]
	//------------------------------------------------------------------------------------------------------------------
	/// @brief      Wake the worker to drain its input/callable queues — COALESCED (F1): a burst of enqueues posts ONE
	///             drain, not N. UAF-safe: posts under m_loopMutex so it never targets a loop that is unwinding.
	/// @details    m_drainPending is set atomically; only the transition false→true posts the queued drain. The drain
	///             clears the flag before draining, so an item pushed after that is guaranteed either drained by this
	///             pass or to re-arm a fresh wake. A no-op if the worker has stopped (the loop pointer is cleared).
	//------------------------------------------------------------------------------------------------------------------
	void wake()
	{
		if (m_drainPending.exchange(true, std::memory_order_acq_rel))
			return;    // a drain is already scheduled — coalesce (no second wake)
		const std::lock_guard<std::mutex> guard(m_loopMutex);
		if (!m_loop)
		{
			m_drainPending.store(false, std::memory_order_release);
			return;    // stopped — nothing to drain onto
		}
		QMetaObject::invokeMethod(m_loop, [this] { drainInput(); }, Qt::QueuedConnection);
	}

	//------------------------------------------------------------------------------------------------------------------
	//      FUNCTION: drainInput [private]  (runs on the worker)
	//------------------------------------------------------------------------------------------------------------------
	/// @brief      Consume everything currently queued (input items via the handler, callables by invoking), FIFO, on the
	///             worker. Non-blocking `try_pop` so it never stalls the loop; clears the coalesce flag first so a
	///             concurrent enqueue re-arms a fresh wake rather than being missed.
	//------------------------------------------------------------------------------------------------------------------
	void drainInput()
	{
		m_drainPending.store(false, std::memory_order_release);    // re-open coalescing before draining (no lost wake)
		// SETUP GATE: never run a submitted closure before setup created the worker-owned state it touches. In NonBlocking
		// mode a submission's onPush→wake() posts a queued drainInput that can be serviced BEFORE the setup singleShot
		// fires; draining then would invoke an owner closure (e.g. disarmTimers) against a not-yet-created timer/socket — a
		// null-deref crash. Leaving the items queued is safe and lossless: the setup singleShot opens the gate and calls
		// drainInput() itself, and any push after that re-arms a fresh wake, so nothing is dropped or reordered.
		if (!m_setupComplete.load(std::memory_order_acquire))
			return;
		while (auto sub = m_submissions.try_pop())                  // ONE lane, drained in true submission order
		{
			if (sub->index() == 1)                                 // a runOnThread callable
			{
				runGuarded(std::get<1>(*sub));
			}
			else if constexpr (HasInput)                            // a typed datum (only when In != void)
			{
				if (m_inputHandler)
				{
					InElement& ref = std::get<0>(*sub);
					runGuarded([this, &ref] { m_inputHandler(ref); });
				}
			}
		}
	}

	//------------------------------------------------------------------------------------------------------------------
	//      FUNCTION: notifyOutput [private]  (called on the pushing thread; delivers on the owner thread)
	//------------------------------------------------------------------------------------------------------------------
	/// @brief      The `output` queue's onPush callback: notify the owner that output is ready — COALESCED — posting a
	///             drain onto the OWNER's thread. If no handler is registered, the queued drain leaves the output intact
	///             for late handler registration or manual `outputs()` consumption.
	/// @details    The drain is posted to @ref m_outputDrainTarget — a QObject THIS QEventThread owns, with owner-thread
	///             affinity — NOT to QCoreApplication::instance(). ~QEventThread destroys the target after the worker
	///             join, so Qt purges any drain metacall still queued on the owner thread and drainOutput can never run
	///             against a destroyed QEventThread (#413). Targeting the app object (app-lifetime) let a queued drain
	///             outlive `this` and read freed members — the use-after-free this replaces.
	//------------------------------------------------------------------------------------------------------------------
	void notifyOutput()
	{
		if constexpr (HasOutput)
		{
			if (m_outputDrainPending.exchange(true, std::memory_order_acq_rel))
				return;    // coalesce
			if (!m_outputDrainTarget)
			{
				m_outputDrainPending.store(false, std::memory_order_release);
				return;
			}
			QMetaObject::invokeMethod(m_outputDrainTarget.get(), [this] { drainOutput(); }, Qt::QueuedConnection);
		}
	}

	//------------------------------------------------------------------------------------------------------------------
	//      FUNCTION: drainOutput [private]  (runs on the owner thread)
	//------------------------------------------------------------------------------------------------------------------
	/// @brief      Deliver every pushed output to the registered handler, FIFO, on the owner's thread, guarded.
	/// @details    Handler state is inspected only here on the owner thread, avoiding cross-thread access while a worker
	///             emits concurrently with late registration.
	//------------------------------------------------------------------------------------------------------------------
	void drainOutput()
	{
		if constexpr (HasOutput)
		{
			m_outputDrainPending.store(false, std::memory_order_release);
			if (!m_outputHandler)
				return;    // late registration will drain the preserved backlog on the owner thread
			while (auto item = m_output.try_pop())
			{
				Out& ref = *item;
				runGuarded([this, &ref] { m_outputHandler(ref); });
			}
		}
	}

	//------------------------------------------------------------------------------------------------------------------
	//      FUNCTION: runGuarded [private]
	//------------------------------------------------------------------------------------------------------------------
	/// @brief      Run @p work under a catch-all so no C++ exception escapes the worker thread; a captured exception is
	///             logged and marshaled to the main thread, then re-thrown there so logerr's crash owner surfaces it.
	/// @tparam     Work  a nullary invocable (the worker callback being guarded).
	/// @param[in]  work  the callback to run guarded.
	/// @details    An exception unwinding out of the worker's `exec()` would `std::terminate` the whole process from a
	///             background thread. Instead it is captured, LOGGED via logerr at the worker (so it is recorded even if
	///             the process is torn down before the marshaled re-throw runs), and re-thrown on the thread that owns
	///             the application object (the main thread) — where logerr's `notify()` override turns it into an orderly
	///             crash dialog + stack trace. Without an application it remains pending for `LOGERR_RETHROW()`. `noexcept`:
	///             the guard itself must never throw.
	//------------------------------------------------------------------------------------------------------------------
	template<typename Work>
	void runGuarded(Work&& work) noexcept
	{
		try
		{
			work();
		}
		catch (...)
		{
			const std::exception_ptr captured = std::current_exception();
			try
			{
				std::rethrow_exception(captured);
			}
			catch (const std::exception& e)
			{
				LOGERR << "QEventThread worker caught: " << e.what() << ENDL;    // recorded before the marshal
			}
			catch (...)
			{
				LOGERR << "QEventThread worker caught a non-std exception" << ENDL;
			}

			logerr::captureException(captured);
			if (QCoreApplication* app = QCoreApplication::instance())
				// Wake the application dispatcher without throwing through Qt internals. logerr's notify() override consumes
				// and rethrows the published failure after QCoreApplication::notify() has returned.
				QMetaObject::invokeMethod(app, [] {}, Qt::QueuedConnection);
		}
	}

	//----------------------------
	//  MEMBERS
	//----------------------------

	SetupFunctionStore          m_setup;                      ///< worker-body setup (runs once on the worker)
	InputHandler                m_inputHandler;               ///< per-datum handler (ON_DATA_RECEIVED), dispatched by drainInput
	OutputHandler               m_outputHandler;              ///< per-output handler (output workers), if registered
	QNotifyingQueue<Submission> m_submissions;                ///< the ONE ordered input lane: enqueue'd data + runOnThread callables, in submission order; onPush wakes the worker
	WorkerInput                 m_input{this};                ///< the `input` handle passed to setup (forwards onDataReceived to this)
	OutputQueue                 m_output;                     ///< OUTPUT queue (notifies on push; only used when Out!=void)
	std::atomic_bool            m_setupComplete{false};       ///< the setup body has run + created its worker-owned objects; gates drainInput so no submitted closure touches not-yet-created state (the NonBlocking stop/drain-before-setup race)
	std::atomic_bool            m_drainPending{false};        ///< coalesces input wakes (one drain per burst)
	std::atomic_bool            m_outputDrainPending{false};  ///< coalesces output notifications
	std::mutex                  m_loopMutex;                  ///< serializes wake() against the worker publishing/clearing m_loop
	QEventLoop*                 m_loop = nullptr;             ///< the worker's live loop (for wake(), under m_loopMutex)
	std::atomic_bool            m_ready{false};               ///< startup handshake: released once the loop is live + setup ran
	std::atomic_bool            m_running{false};             ///< true only while the worker function is alive
	/// The OWNER-thread target the output-drain metacall is posted to (created on the owner thread when HasOutput). It is
	/// destroyed in ~QEventThread AFTER the worker join, which purges any drain metacall still queued on the owner thread
	/// — so drainOutput can never run against a destroyed QEventThread. Posting to QCoreApplication::instance() instead
	/// (app-lifetime) let a queued drain outlive `this` and fire against freed members — the #413 use-after-free. This
	/// makes the OUTPUT lane's teardown symmetric with the INPUT lane (whose worker-loop metacalls die on the join).
	std::unique_ptr<QObject>    m_outputDrainTarget;
	std::jthread                m_thread;                     ///< the worker; declared LAST so it is joined FIRST in the dtor
};

//----------------------------
//  MACROS
//----------------------------

/// Declare a QEventThread's setup body with ZERO signature boilerplate, capturing `[this]` (the member-of-a-class use).
/// In scope: `eventLoop` (QEventLoop*), `stop` (std::stop_token), `input` (the input queue ref), `output` (the notifying
/// output queue ref). Parent worker objects to `eventLoop`; push results to `output` (or `EMIT(x)`).
///
///     QEventThread<Request, Reply> m_thread = THREAD_SETUP({ m_sock = new QSslSocket(eventLoop); ON_DATA_RECEIVED({...}); });
///
/// Yields a generic lambda the templated (non-explicit) constructor wraps, so `= THREAD_SETUP({…})` copy-inits the
/// member. For a body that must capture something other than `this` (a free function, a test capturing locals), use
/// THREAD_SETUP_CAPTURE.
#define THREAD_SETUP(...) THREAD_SETUP_CAPTURE([this], __VA_ARGS__)

/// THREAD_SETUP with an explicit capture list — for a worker declared outside a class (free function, test) or one that
/// needs to capture locals: `THREAD_SETUP_CAPTURE([&], { … })`. `eventLoop`/`stop`/`input`/`output`/`connectGuarded`
/// are injected always. `connectGuarded(sender, signal, slot)` runs a signal callback on this event loop under the same
/// exception boundary as setup/input work, allowing ERR from the callback to surface safely on the application thread.
#define THREAD_SETUP_CAPTURE(capture, ...)                                                                     \
	(capture(QEventLoop* eventLoop, ::std::stop_token stop, auto& input, auto& output) {                       \
		auto connectGuarded = [inputHandle = &input](auto* sender, auto signal, auto&& slot) {                   \
			return inputHandle->connectGuarded(sender, signal, ::std::forward<decltype(slot)>(slot));             \
		};                                                                                                      \
		(void) eventLoop;                                                                                      \
		(void) stop;                                                                                           \
		(void) input;                                                                                          \
		(void) output;                                                                                         \
		(void) connectGuarded;                                                                                 \
		__VA_ARGS__                                                                                            \
	})

/// Push a result to the worker's `output` queue (which notifies the owner). Terse sugar for `output.push(x)`, usable
/// anywhere in a THREAD_SETUP body — a data handler, a socket slot, a timer — 0, 1, or N times.
#define EMIT(...) output.push(__VA_ARGS__)

/// Register the per-input handler inside a THREAD_SETUP body with NO lambda/arg boilerplate; the handler lambda captures
/// `[&]` (so it sees `this` AND the injected `input`/`output`). Each enqueued datum is delivered — FIFO, on the worker,
/// guarded — as a reference named `data`:
///
///     ON_DATA_RECEIVED({ EMIT(process(data)); });
///
/// Requires an input worker (QEventThread<In, …>, In != void); `data` is an `In&`. Registers on the input queue that the
/// enclosing setup body owns, via the `input` in scope — no handle to the thread. Use ON_DATA_RECEIVED_CAPTURE for a
/// different handler capture (it MUST still capture `output` to EMIT — `[&]` does).
#define ON_DATA_RECEIVED(...) ON_DATA_RECEIVED_CAPTURE([&], __VA_ARGS__)

/// ON_DATA_RECEIVED with an explicit handler-capture list. `data` is the item; the capture MUST include `output` (and
/// anything else the body uses) to EMIT — `[&]` captures all.
#define ON_DATA_RECEIVED_CAPTURE(capture, ...)                                     \
	do                                                                             \
	{                                                                              \
		auto _qet_handler = capture(auto& data) __VA_ARGS__;                       \
		input.onDataReceived(::std::move(_qet_handler));                           \
	} while (false)

#endif    // QEVENTTHREAD_H
