/// @file signals.h
/// @brief Industrial signal regime for the UI server. A web server
/// that a client disconnect can kill (via SIGPIPE) is a trivial remote
/// denial of service; a server that segfaults without a trace is
/// undebuggable. This module installs the defensive regime the
/// hardening effort calls for:
///
///   - **SIGPIPE -> ignored.** The single highest-value handler for a
///     web server: a client that disconnects mid-write on an HTTP or
///     WebSocket connection raises SIGPIPE, whose default action is to
///     *kill the process*. Ignoring it turns the failed write into a
///     normal `EPIPE`/`ECONNRESET` the socket layer already handles.
///   - **Fault signals (SEGV/ABRT/BUS/ILL/FPE) -> diagnose then die.**
///     An async-signal-safe handler writes a banner, the in-flight
///     request context for the faulting thread, and a backtrace to
///     stderr, then re-raises the signal under the default handler so
///     the kernel still produces a core dump. `try/catch` cannot catch
///     these — a null-deref in a handler is a SIGSEGV, not an
///     exception — so this is how such crashes get diagnosed.
///   - **SIGUSR2 -> status.** Sets a flag a supervisor loop polls to
///     dump stats. **SIGHUP -> reopen/flush logs.** Same mechanism.
///
/// Graceful TERM/INT shutdown is delegated to Crow's own `run()` loop,
/// which installs handlers that stop the app and drain connections.
// Copyright (c) 2026 Einheit Networks

#ifndef INCLUDE_EINHEIT_UI_SIGNALS_H_
#define INCLUDE_EINHEIT_UI_SIGNALS_H_

#include <string_view>

namespace einheit::ui {

/// Which parts of the regime to install. All on by default.
struct SignalConfig {
  /// Ignore SIGPIPE. Leave true for any networked server.
  bool ignore_sigpipe = true;
  /// Install async-safe SEGV/ABRT/BUS/ILL/FPE diagnostics.
  bool fault_handlers = true;
  /// Install SIGUSR2 -> status-request flag.
  bool status_on_usr2 = true;
  /// Install SIGHUP -> reload/flush-logs flag.
  bool reload_on_hup = true;
};

/// Install the framework's signal regime. Idempotent — calling twice
/// re-applies the same dispositions. Call once at process startup,
/// before the server begins accepting connections.
/// @param cfg Which handlers to install.
auto InstallSignalHandlers(const SignalConfig &cfg = {}) -> void;

/// Record the request currently being served on the calling thread so
/// a fault handler can report it if this thread crashes. The strings
/// are copied into a fixed per-thread buffer (bounded; async-signal-
/// safe to read from the handler). Call at the top of a request; the
/// framework's Crow middleware does this automatically.
/// @param method HTTP method (e.g. "GET").
/// @param target Request target (e.g. "/runs/12/steps/3").
auto SetCurrentRequest(std::string_view method, std::string_view target)
    -> void;

/// Clear the calling thread's recorded request. Optional — the next
/// SetCurrentRequest overwrites it — but keeps the fault report from
/// naming a stale request on an idle worker thread.
auto ClearCurrentRequest() -> void;

/// True (once) if SIGUSR2 has been received since the last call, and
/// clears the pending flag. Poll from a supervisor/status loop.
auto ConsumeStatusRequest() -> bool;

/// True (once) if SIGHUP has been received since the last call, and
/// clears the pending flag. Poll to reopen/flush logs.
auto ConsumeReloadRequest() -> bool;

}  // namespace einheit::ui

#endif  // INCLUDE_EINHEIT_UI_SIGNALS_H_
