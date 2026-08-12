# Design spec: restore the "debugging gold" — traced LOGERR + guarded QEventThread slots

Status: IMPLEMENTED (logerr repo, 2026-08-12). Two related changes so that **every error logs its
diagnostic context (file/function/line, and where possible a stack trace)** and so that
an error raised on a `QEventThread` worker can surface it **without terminating the
process**. Motivated by a consuming app (Helen) having lost the trace/location that made
`logerr` valuable: most error sites logged via the formerly bare `LOGERR` rather than
throwing via `ERR`, and a throw from an unguarded worker signal-slot terminates.

Terminology used below matches the current source: `logerr/include/logerrMacros.h`
(the `LOG*`/`ERR` macros), `logerr/include/StackTrace.h` (the trace
functor), `qlogerr/include/QEventThread.h` (the worker primitive + `runGuarded`),
`qlogerr/src/Application.cpp` (`Application::notify` — the GUI-thread exception sink).

---

## Background: how the two error paths differed before this change

- `ERR(msg)` -> `throw logerr::exception(msg, __FILENAME__, LOGERR_FUNCTION, __LINE__)`.
  The thrown `StackTraceException` carries **file + function + line + a stack trace**.
  On the GUI thread this unwinds into `Application::notify`, which shows the
  `ExceptionDialog` and logs the trace. THIS is the diagnostic "gold."
- `LOGERR` -> `std::cout << "[" << TimestampLite() << "] [" << APPINFO::name() << "] [ERROR]    "`.
  A bare log line: **no file, no line, no function, no trace.**

So an error logged with `LOGERR` (the common case — a log-then-degrade path, an
error-isolation handler, an outcome-carrying worker callback, a pre-`exec()` startup
path) is diagnostically blind. Converting those sites to `ERR` is usually WRONG: a
throw there skips a `return`-contract the caller depends on, skips cleanup, tears down
an isolation boundary, or (on a worker thread) terminates the process. The fix is not
to throw more — it is to make the NON-throwing log carry the gold, and to make the
worker path able to throw safely when a fault genuinely is fatal-worthy.

---

## Change 1 — `LOGERR` (and `LOGWARNING`) carry file/function/line

`__LINE__`/`__FILE__`/`LOGERR_FUNCTION` expand at the macro's textual call site, so a
prefix that streams them records the **accurate** location with zero call-site edits
and no change to the `LOGERR << a << b << ENDL` streaming form.

Proposed (in `logerrMacros.h`, keeping the existing `#ifndef` override guard so a
consumer can still redefine it, e.g. Helen's per-module-tagged `helenLogModule.h`):

```cpp
#ifndef LOGERR
#define LOGERR                                                                 \
    (std::cout << '[' << TimestampLite() << "] [" << APPINFO::name()           \
               << "] [ERROR]   [" << __FILENAME__ << ':' << __LINE__ << ' '    \
               << LOGERR_FUNCTION << "]  ")
#endif
```

- Only `LOGERR` (and, if desired, `LOGWARNING`) gain the location; `LOGINFO`/`LOGDEBUG`
  stay clean (normal/expected events, no location noise).
- Column widths / the log-parser: adding a `[file:line function]` field changes the
  line shape. Any consumer that parses the log by fixed columns must be updated; prefer
  a delimiter the parser already tolerates. Confirm against the app's log reader
  (Helen's `logDock`/`LogModel`) before release.
- `helenLogModule.h`-style overrides (`#undef LOGERR` + module-tagged redefine) must be
  updated in lockstep to include the same location field, or module-tagged files lose
  it. That is a consumer-side follow-up, tracked at the bump.

### Optional: a full non-throwing stack trace on demand

A full stack trace cannot ride the streaming `LOGERR << ...` prefix (the trace must
come AFTER the streamed message, which the prefix macro cannot control). `StackTrace`
is already a non-throwing functor (`StackTrace.h`: "generates a stack trace from the
point where it was called", `operator std::string()`), so add a dedicated macro for the
cases that want the whole trace without throwing:

```cpp
// Log an error line WITH a full stack trace, without throwing (no terminate, no unwind).
// The trace is captured at THIS call site (StackTrace must be constructed at the site).
#ifndef LOGERR_TRACE
#define LOGERR_TRACE(msg)                                                      \
    (LOGERR << (msg) << '\n' << static_cast<std::string>(::StackTrace(1)) << ENDL)
#endif
```

Use `LOGERR_TRACE` at a non-throwable site that is nonetheless a genuine fault worth a
trace (a worker callback that must not throw, an error-isolation handler). `LOGERR`
alone (with the file:line prefix) remains the default; `LOGERR_TRACE` is opt-in where
the full trace earns its cost.

---

## Change 2 — guard `QEventThread` signal-slot callbacks (surface, don't terminate)

### The gap
`QEventThread::runGuarded` (`QEventThread.h`) wraps the setup body, every input handler,
every output handler, and `runOnThread()` in `try/catch(...)`. It publishes the captured
exception through logerr's synchronized exception slot and queues a no-op wake to the
application thread. `Application::notify` consumes and rethrows it only after Qt's
dispatcher returns, so the dialog/trace surfaces without throwing through Qt internals
or terminating the worker.

But a worker that does a **raw** `connect(sender, &Sender::signal, receiver, lambda)`
inside its setup body runs that lambda directly from the worker's bare
`eventLoop.exec()` — NOT through `runGuarded`. The header's own doc block already flags
this (a RAW `connect` slot is unguarded). A throw from such a slot escapes `exec()` and
terminates. This is why consuming code puts socket `errorOccurred` / `sslErrors` /
accept-error handling in `LOGERR` (it can't throw): the error is real and
report-worthy, but there is no safe way to raise it from that slot today.

### The fix: a guarded connect helper
The setup macro injects `connectGuarded(sender, signal, slot)`. Internally the setup's
`WorkerInput` handle connects the signal with the private worker event loop as its Qt
context and wraps the slot with the owning `QEventThread::runGuarded`. This avoids
referring to the `QEventThread` member while that member is still being constructed,
enforces worker affinity even when the sender emits from another thread, and ties the
connection lifetime to the event loop.

- Worker code replaces a raw `connect(m_socket, &QAbstractSocket::errorOccurred, ...)`
  with `connectGuarded(m_socket, &QAbstractSocket::errorOccurred, ...)`. Now the slot may
  call `ERR(...)` for a genuine, fatal-worthy fault and it will surface on the GUI thread
  without terminating.
- `runGuarded` is `noexcept` and already handles the marshal + the SEH caveat (an SEH
  access violation under `/EHsc` is not caught by `catch(...)`; that is a separate,
  documented limitation, unchanged here).
- This does NOT mean every worker socket error becomes `ERR`. Most stay non-throwing:
  a transient reconnect blip (RemoteHostClosed/ConnectionRefused) is `LOGINFO`; an
  outcome-carrying callback that must `emit bound()`/`emit listening()` still needs to
  deliver that outcome (a throw would skip it and hang the owner). `connectGuarded` only
  makes it *possible* to raise `ERR` from a slot where a fault genuinely warrants the
  dialog + trace and there is no outcome to deliver.

---

## Consumer-side notes (for the app that vendors logerr, e.g. Helen)

- After a logerr bump carrying Change 1, `helenLogModule.h` (and any `#undef LOGERR`
  override) must mirror the new location field, or module-tagged files lose it.
- The **Covenant** in a Node/agent app: a REMOTE (buoy/agent) error must be surfaced on
  the controlling **Node**, never thrown on the headless agent (no GUI, no dialog — a
  throw there just hits the agent's `std::set_terminate` handler). The agent logs +
  relays the failure over the wire (an `ErrorMsg`); the Node's receive handler is where
  the traced `ERR` (or the already-present per-ship Error surface) belongs. Neither
  change here alters that: `connectGuarded`/`LOGERR_TRACE` are for surfacing an error on
  the host that OWNS the GUI, wherever the worker runs.
- A pre-`QApplication::exec()` error (startup, before `Application::notify` is
  dispatching) is still not caught by the notify sink — those stay `LOGERR` (or need a
  top-level `try/catch` in `main()`), unchanged by this spec.

---

## Consumer rollout

1. Cut a logerr release containing the implemented location prefix, `LOGERR_TRACE`, and
   `connectGuarded`; the consumer bumps its vendored bundle and updates its `LOGERR` overrides.
2. The consumer migrates its worker `connect(...)` sites for socket/TLS error slots to
   `connectGuarded(...)`, then converts the genuinely-fatal-worthy ones to `ERR`.
3. Neither change requires a call-site edit to keep working: existing `LOGERR << ...`
   and existing raw `connect(...)` compile unchanged; the improvements are opt-in
   (the location prefix is automatic and additive; `LOGERR_TRACE`/`connectGuarded` are
   new, used where wanted).
