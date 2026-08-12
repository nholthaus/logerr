# QEventThread

A small, standalone, header-only primitive: a **background thread that runs its own private `QEventLoop`**, packaged so
a component can own Qt objects (timers, sockets, `QProcess`, models) off the GUI thread — with optional thread-safe
**input** and **output** data queues — **without re-writing (and re-mis-writing) the jthread + event-loop +
startup-handshake + bounded-teardown + no-exceptions-escape boilerplate every time.**

You **compose** it (hold one as a member). You never derive from it, and you never touch `QThread` / `moveToThread`.

```cpp
class LinkPrivate : public QObject
{
    Q_OBJECT
    QSslSocket* m_sock = nullptr;                          // members the worker touches — declared BEFORE the thread

    QEventThread<Request, Reply> m_thread = THREAD_SETUP({
        m_sock = new QSslSocket(eventLoop);                // born on the worker, parented to its loop
        connectGuarded(m_sock, &QSslSocket::readyRead,
                       [this] { /* may use ERR; parse frames, EMIT(reply) */ });
        ON_DATA_RECEIVED({ m_sock->write(encode(data)); });// a Request enqueued from any thread → sent on the worker
    });                                                    // m_thread declared LAST → joined FIRST in the dtor

public:
    void send(Request r) { m_thread.enqueue(std::move(r)); }
    // owner consumes replies via m_thread.onOutput(...) / outputs()
};
```

That one member replaces ~40 lines of hand-rolled worker plumbing and makes the recurring threading bugs *impossible to
write*.

---

## The two things it gives you

### 1. `QNotifyingQueue<T>` — a reactive FIFO (also usable on its own)

A thread-safe `concurrent_queue<T>` that **notifies on every push over two independent channels**, so a consumer never
polls:

- an **`onPush` callback** (the async channel), fired on the pushing thread after each enqueue, and
- the underlying **condition variable**, so a plain (non-Qt) consumer thread can BLOCK on **`wait_pop_for(dest, timeout)`**.

A single `push`/`emplace` wakes both; the producer never blocks. `QEventThread`'s `output` is a `QNotifyingQueue`; its
**input is ONE ordered lane** (a `variant<In, callable>` FIFO) that `enqueue()` and `runOnThread()` share — so the two
drain in true **submission order** (a `runOnThread` issued before an `enqueue` runs before it, and vice versa). There is
no separate callable lane and no cross-lane inversion.

### 2. `QEventThread<In = void, Out = void>` — the worker

Both template parameters are optional:

| Shape | Meaning |
|---|---|
| `QEventThread<>` | **self-driving** worker — owns a socket/timer, reacts to its own events; occasional `runOnThread()` one-offs. No data queues. |
| `QEventThread<In>` | **input** worker — producers `enqueue(In)`; the worker consumes each via `ON_DATA_RECEIVED`. |
| `QEventThread<In, Out>` | **full-duplex** — `enqueue(In)` in, worker `EMIT(Out)`s out; owner consumes via `onOutput`/`outputs()`. |

---

## Inside a `THREAD_SETUP` body — the injected surface

The setup body runs **once, on the worker thread**, and receives five names (no handle back to the thread object):

| name | type | use |
|---|---|---|
| `eventLoop` | `QEventLoop*` | **parent every object you create to it** (`new T(eventLoop)`) — born on the worker, reaped on the worker at unwind (RAII). |
| `stop` | `std::stop_token` | poll for a bounded graceful teardown (e.g. from a repeating timer). |
| `input` | a worker handle | register the per-datum handler via `ON_DATA_RECEIVED` (`input.onDataReceived(...)`) when `In != void`; it also backs `connectGuarded`. |
| `output` | `QNotifyingQueue<Out>&` | push results to it; **each push notifies the owner**. `EMIT(x)` is sugar for `output.push(x)`. Present when `Out != void`. |
| `connectGuarded` | generic callable | connect a Qt signal to a worker lambda under the exception boundary; `ERR`/throws surface on the application thread instead of escaping the event loop. |

**Emission is triggered by the enqueue itself** — a handler (or a socket slot, or a timer) may `EMIT` **0, 1, or N**
times, from anywhere on the worker, or not at all (e.g. it just writes to disk).

### Macros

- **`THREAD_SETUP({ … })`** — declare the setup body capturing `[this]` (the member-of-a-class use). Yields the callable
  the constructor takes, so `= THREAD_SETUP({…})` copy-inits the member.
- **`THREAD_SETUP_CAPTURE([&], { … })`** — same, with an explicit capture list (a free function / test capturing locals).
- **`ON_DATA_RECEIVED({ … })`** — register the per-input handler; the item is a reference named **`data`** (`In&`),
  delivered FIFO on the worker, guarded. Handler lambda captures `[&]` (sees `this` + the injected `input`/`output`).
- **`ON_DATA_RECEIVED_CAPTURE([&], { … })`** — same, explicit capture (must capture `output` to `EMIT`; `[&]` does).
- **`EMIT(x)`** — push `x` to `output` (notifying the owner). Usable anywhere in the body.
- **`connectGuarded(sender, signal, lambda)`** — like the context-taking `QObject::connect`, but always targets the
  worker event loop and catches any C++ exception from the lambda for application-thread surfacing.

---

## Public API (member functions)

Header: `#include "QEventThread.h"` — the CMake target is `QEventThread` (INTERFACE lib, Qt::Core only; the header also
uses `concurrent_queue.h`).

| Member | Availability | What it does |
|---|---|---|
| `QEventThread(setup)` | all | Start the worker and **block until the loop is live and `setup` has run** (an atomic-wait handshake — not a spin). |
| `void enqueue(In)` | `In != void` | Hand an item to the worker (FIFO, thread-safe, coalesced wake). A no-op once stopped. |
| `void runOnThread(fn)` | all | Run a one-off nullary callable on the worker (guarded). The occasional "do this over there." Ordered WITH `enqueue()` — same lane, true submission order. |
| `void onOutput(fn)` | `Out != void` | Register a `void(Out&)` handler from the thread that constructed the worker; each emitted item is delivered on that owner thread, FIFO, guarded. |
| `OutputQueue& outputs()` | `Out != void` | The output queue, to drain yourself (`while (outputs().try_pop(r)) …`) or block on (`outputs().wait_pop_for(r, t)`). |
| `void onDataReceived(fn)` | `In != void` | Register the per-input handler directly (the `ON_DATA_RECEIVED` macro is the sugar). |
| `void requestStop()` | all | Ask the worker to stop and quit its loop, without waiting. Idempotent; the dtor also does this. |
| `bool isRunning()` | all | Whether the worker function is currently alive. A non-blocking worker may briefly report false during startup. |
| `~QEventThread()` | all | Request stop and **join as a lifetime barrier** — no worker callback runs after a member it reads is destroyed. Pending submissions may be discarded; enqueue and wait for an ordered barrier first when flushing is required. |

---

## Guarantees (the bugs it removes)

| Recurring bug | How `QEventThread` removes it |
|---|---|
| **Wrong thread affinity** — an object parented to a GUI object (or `moveToThread`'d) is torn down on the wrong thread → the UAF-guard plague | You only ever get a `QEventLoop*` to parent to; the loop **reaps its children on the worker** at unwind. No `moveToThread`. |
| **Dropped early work** — a call right after construction races a not-yet-running loop | The constructor **blocks until the loop is live and `setup` ran**. |
| **Teardown deadlock** — a stop requested before the loop was live is lost; `join()` hangs | The `stop_token` is the sole exit SSOT; a `std::stop_callback` quits the loop **synchronously**. The dtor joins as a barrier. |
| **UAF on teardown** — a callback runs against a destroyed member | The dtor **joins before any member is destroyed**; `wake()` is serialized against loop teardown so it can never target a half-destroyed loop. |
| **A worker throw kills the process** — an uncaught exception unwinding out of a thread calls `std::terminate` | `setup`, every input handler, every `runOnThread()`, and every `connectGuarded` lambda run under `catch (...)`; a captured exception is atomically published to logerr and a no-op event wakes the main thread so the application exception boundary can consume it after Qt dispatch returns. |

**F1-grade:** no busy waits, no avoidable blocks — the handshake is a futex atomic-wait, wakes are **coalesced** (a burst
of enqueues → one drain), drains are non-blocking `try_pop`, and the ONLY block is the dtor join.

---

## The rules (follow these and it just works)

1. **Declare the `QEventThread` member LAST**, after every member its callbacks touch — so it is joined first, while
   those members are still alive.
2. **Create worker objects only inside the setup body, parented to `eventLoop`.** Never `new` on the caller thread and hand
   it in; never `moveToThread`.
3. **Cross into the worker with `enqueue` / `runOnThread`**, or wire a signal during setup. Use `connectGuarded` when
   its lambda can throw; a raw `QObject::connect(..., eventLoop, lambda)` remains the lower-overhead option for a
   provably non-throwing callback. Never call a worker object's method directly from another thread.
4. **Snapshot by value.** A worker callable must be self-contained (capture PIDs/handles/bytes by value). Cross-thread
   scalars are `std::atomic`; shared buffers get a `std::mutex`.

---

## Worked shapes

**Self-driving (a UDP listener):**
```cpp
QEventThread<> m_thread = THREAD_SETUP({
    m_socket = new QUdpSocket(eventLoop);
    m_socket->bind(QHostAddress::LocalHost, 9000);
    connectGuarded(m_socket, &QUdpSocket::readyRead, [this] { emit received(readAll()); });
});
```

**Full-duplex request/reply:**
```cpp
QEventThread<Request, Reply> m_thread = THREAD_SETUP({
    ON_DATA_RECEIVED({ EMIT(process(data)); });   // 1-in-1-out
});
// ...
m_thread.onOutput([this](Reply& r) { updateUi(r); });   // delivered on the GUI thread
```

**Fan-out / filter (0..N outputs per input):**
```cpp
ON_DATA_RECEIVED({
    if (isHeartbeat(data)) return;                // emit nothing
    for (auto& chunk : split(data)) EMIT(chunk);  // emit many
});
```

**Two workers piped (producer → consumer, event-driven, no blocking):**
```cpp
encoder.outputs().onPush([&] { Frame f; while (encoder.outputs().try_pop(f)) sender.enqueue(std::move(f)); });
```

**Bare-thread consumer (the CV channel):**
```cpp
std::thread consumer([&] { Reply r; while (running && worker.outputs().wait_pop_for(r, 200ms)) handle(r); });
```

---

## Migrating an existing hand-rolled worker

Helen has the jthread + private-`QEventLoop` pattern hand-copied across `lib/transport`, `lib/hawser`,
`QNativeProcess`, telemetry, etc. Every copy re-implements — and is a fresh chance to mis-implement — the startup
handshake, the `stop_callback`, the pre-loop-stop guard, and the dtor join. `QEventThread` absorbs all of it. This is the
canonical before/after every refactor follows.

### The mechanical mapping

| Hand-rolled boilerplate | Replaced by |
|---|---|
| `std::jthread m_thread` + `run(std::stop_token)` | the `QEventThread` member (declared LAST) |
| `std::atomic_bool m_ready` + `std::mutex m_readyMutex` + `std::condition_variable m_readyCondition` + the `m_ready.wait` | the constructor's built-in handshake (delete all of it) |
| `#define RUN_WHEN_EVENT_LOOP_STARTS(...)` + the `QTimer::singleShot(0ms, …)` init | the `THREAD_SETUP({ … })` body (runs once, on the worker, after the loop is live) |
| `std::stop_callback stopQuit(...)` + `connect(this, &Foo::stopRequested, …, quit)` + the pre-loop `if (stopToken.stop_requested()) eventLoop.quit()` guard | nothing — `QEventThread` owns the stop→quit SSOT |
| `QEventLoop eventLoop; … eventLoop.exec();` | nothing — the worker runs it for you |
| `~Foo() { m_thread.request_stop(); if (joinable()) join(); }` | nothing — `~QEventThread()` is the join barrier |
| a `concurrent_queue<Work>` drained in an `onReadyToSend` slot + a `readyToSend` signal to poke the worker | `enqueue()` + `ON_DATA_RECEIVED({ … })` (input), or push to `output` / `EMIT` (output) |
| worker→owner results via a bespoke `readyRead()` signal + a `m_receiveQueue` the owner polls | the `output` queue (`onOutput` / `outputs()` / `wait_pop_for`) |

### Before — `UdpSocketPrivate` (abridged; the real file is ~120 h + ~200 cpp lines)

```cpp
// udpSocketPrivate.h
class UdpSocketPrivate : public QObject {
    Q_OBJECT
    // ... send/receive queues ...
    std::atomic_bool        m_ready = false;          // ─┐ hand-rolled startup handshake
    std::mutex              m_readyMutex;              //  │
    std::condition_variable m_readyCondition;          // ─┘
    std::jthread            m_thread;                  // declared LAST
    void run(std::stop_token);
signals:
    void readyToSend(QPrivateSignal);                  // poke the worker to drain the send queue
    void stopRequested();
};

// udpSocketPrivate.cpp
#define RUN_WHEN_EVENT_LOOP_STARTS(...) QTimer::singleShot(0ms, &eventLoop, __VA_ARGS__)
UdpSocketPrivate::UdpSocketPrivate(...) { m_thread = std::jthread([this](std::stop_token s){ run(s); }); /* + m_ready.wait */ }
UdpSocketPrivate::~UdpSocketPrivate() { m_thread.request_stop(); if (m_thread.joinable()) m_thread.join(); }
void UdpSocketPrivate::run(std::stop_token stopToken) {
    QEventLoop eventLoop;
    auto* socket = new QUdpSocket(&eventLoop);
    QObject::connect(socket, &QUdpSocket::readyRead, &eventLoop, [this, socket]{ readDatagrams(socket); });
    QObject::connect(this, &UdpSocketPrivate::readyToSend, &eventLoop, /* drain m_sendQueue */);
    std::stop_callback stopQuit(stopToken, [&]{ QMetaObject::invokeMethod(&eventLoop, [&]{ eventLoop.quit(); }, Qt::QueuedConnection); });
    RUN_WHEN_EVENT_LOOP_STARTS([this, socket, &eventLoop, stopToken]{
        if (stopToken.stop_requested()) { eventLoop.quit(); return; }   // pre-loop-stop guard
        socket->bind(...); /* set m_ready + notify */
    });
    eventLoop.exec();
}
```

### After — the same worker on `QEventThread`

```cpp
// udpSocketPrivate.h
class UdpSocketPrivate : public QObject {
    Q_OBJECT
    QUdpSocket* m_socket = nullptr;                    // touched only on the worker — declared BEFORE the thread
    QEventThread<Datagram> m_thread = THREAD_SETUP({   // Datagram in; a bound() signal / receiveQueue out
        m_socket = new QUdpSocket(eventLoop);          // born on the worker
        m_socket->bind(m_host, m_port);                // (the singleShot/RUN_WHEN body is just this body now)
        connectGuarded(m_socket, &QUdpSocket::readyRead, [this]{ readDatagrams(m_socket); });
        ON_DATA_RECEIVED({ sendDatagram(m_socket, data); });   // was: readyToSend slot draining m_sendQueue
    });                                                // declared LAST → joined FIRST
public:
    void send(Datagram d) { m_thread.enqueue(std::move(d)); }   // was: queueDataToSend + emit readyToSend
    // ~UdpSocketPrivate is now = default — the QEventThread member's dtor is the join barrier.
};
```

Gone: `m_ready` / `m_readyMutex` / `m_readyCondition`, the `RUN_WHEN_EVENT_LOOP_STARTS` macro, the `stop_callback`, the
`stopRequested` signal, the pre-loop-stop guard, the explicit `run()` + `exec()`, and the hand-written dtor. What
remains is the domain logic: bind, read, send.

### Migration checklist (per file)

1. **Identify the shape.** No inbound work items and it just reacts to its own socket/timer → `QEventThread<>`. Producers
   feed it typed items → `QEventThread<In>`. It also feeds results back → `QEventThread<In, Out>`.
2. **Move object creation into `THREAD_SETUP`**, parented to `eventLoop` (was: `new T(&eventLoop)` inside `run`). Members the
   worker touches stay declared **before** the `QEventThread` member.
3. **Replace the send-queue+poke-signal** with `enqueue` + `ON_DATA_RECEIVED`. Replace the **result path** with
   `EMIT`/`output` + `onOutput`/`outputs()` (or keep a `receive()`+`m_receiveQueue`+`readyRead()` contract verbatim if
   that is the existing behavior — don't move a blocking receive onto the GUI thread via `onOutput`).
4. **Delete** the handshake trio, the `RUN_WHEN_EVENT_LOOP_STARTS` macro, the `stop_callback`, the `stopRequested`
   signal, the pre-loop-stop guard, and the hand-written dtor (make it `= default`).
5. **Keep the public `Foo` (GUI-affinity) → `FooPrivate` (worker owner) PIMPL split** — `QEventThread` replaces only the
   private's thread plumbing; the public class still re-emits the worker's signals.
6. **Any UAF-guard band-aid** (generation stamp, nulled-pointer-checked-after, `destroyed()` handler, "already-freed?"
   flag) should become **deletable** — if it doesn't, the ownership is still wrong; fix the parenting, don't keep the
   guard.
7. **Preserve the exact signal contract** the public class exposes; the refactor is behavior-preserving. Re-run that
   worker's full test file (and add the missing corner-case tests) before declaring it done.

---

## Part of the logerr family

`QEventThread` is a **logerr-family** component (it will live in logerr): it depends on **Qt::Core** and **logerr**.
logerr provides the `concurrent_queue` behind `QNotifyingQueue`, the `ERR`/`LOGERR` diagnostics, and — the reason the
worker-fault contract is *graceful* — the application crash owner. A C++ exception caught in the worker is logged via
logerr at the worker, published through its synchronized exception slot, then consumed on the main thread after Qt's
base event dispatcher returns, where logerr's application `notify()` override turns it into an
orderly crash dialog + stack trace instead of a hard abort from a background thread. Link it with:

```cmake
target_link_libraries(<your-target> PRIVATE QEventThread)   # brings Qt::Core + logerr transitively
```

No customization point, no opt-in adapter — the logerr integration is always on.

---

## What it is NOT

- **Not a thread pool / task queue for CPU work.** It's one long-lived thread with an event loop, for owning
  event-driven Qt objects. For fire-and-forget CPU parallelism use `QtConcurrent` / `std::async`.
- **Not a base class.** Compose it; don't inherit it.
- **Not movable/copyable.** It owns a running thread that captures `this`.
