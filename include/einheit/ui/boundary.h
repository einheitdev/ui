/// @file boundary.h
/// @brief Exception-containment boundary for the UI framework. The
/// framework's hardening stance is "an error is caught at a boundary,
/// logged with full context, and turned into a clean response or a
/// dropped-but-logged event — never a crash." Crow already wraps HTTP
/// route handlers in a try/catch, but every *other* place a callback
/// runs — WebSocket onopen/onmessage/onclose hooks, background sampler
/// and poller threads, the PTY reader thread — runs with no such net.
/// A throw there propagates into Crow's ASIO loop or a bare
/// std::thread and terminates the process.
///
/// `Guard` is that missing net: run a callable, and if it throws, log
/// the throw with a caller-supplied context string and swallow it. It
/// never throws itself, so it is safe to call from a `noexcept`
/// context, a destructor, or a raw thread body.
// Copyright (c) 2026 Einheit Networks

#ifndef INCLUDE_EINHEIT_UI_BOUNDARY_H_
#define INCLUDE_EINHEIT_UI_BOUNDARY_H_

#include <spdlog/spdlog.h>

#include <exception>
#include <string_view>
#include <utility>

namespace einheit::ui {

/// Run `fn`, containing any exception it throws. On a throw the
/// exception is logged at error level with `context` for
/// attribution, then swallowed. Never propagates — safe to call from
/// a thread body, a WebSocket callback, a destructor, or any other
/// place where a throw would otherwise terminate the process.
/// @param context Human-readable attribution (e.g. "ws:/events onopen",
///                "hd-relay sampler", "shell pty reader"). Logged as-is.
/// @param fn Callable taking no arguments.
/// @returns true iff `fn` completed without throwing.
template <typename Fn>
auto Guard(std::string_view context, Fn &&fn) noexcept -> bool {
  try {
    std::forward<Fn>(fn)();
    return true;
  } catch (const std::exception &e) {
    // Defensive: even the log call is guarded — spdlog formatting or
    // a sink write must never be the thing that finally kills us.
    try {
      spdlog::error("[boundary] {} threw: {}", context, e.what());
    } catch (...) {
    }
    return false;
  } catch (...) {
    try {
      spdlog::error("[boundary] {} threw a non-std exception", context);
    } catch (...) {
    }
    return false;
  }
}

}  // namespace einheit::ui

#endif  // INCLUDE_EINHEIT_UI_BOUNDARY_H_
