/// @file signals.cc
/// @brief Signal regime implementation. The fault path is written to
/// the async-signal-safe subset only: no malloc, no stdio, no
/// spdlog — just write(2) into fixed buffers and libc's backtrace().
// Copyright (c) 2026 Einheit Networks

#include "einheit/ui/signals.h"

#include <execinfo.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstring>

namespace einheit::ui {
namespace {

// Per-thread record of the request in flight, for the fault handler.
// A fixed buffer so the handler reads it without allocating. `len` is
// updated last so a concurrent read never sees a longer length than
// the bytes actually written.
constexpr std::size_t kReqBufSize = 256;
thread_local char g_req_buf[kReqBufSize] = {0};
thread_local volatile std::size_t g_req_len = 0;

// Control-signal flags. `sig_atomic_t` is the only integer type the
// standard guarantees safe to touch from a signal handler.
volatile std::sig_atomic_t g_status_pending = 0;
volatile std::sig_atomic_t g_reload_pending = 0;

// --- async-signal-safe primitives -------------------------------

// write(2) all of buf, retrying short writes. Safe in a handler.
void WriteAll(int fd, const char *buf, std::size_t len) {
  std::size_t off = 0;
  while (off < len) {
    const ssize_t n = ::write(fd, buf + off, len - off);
    if (n <= 0) {
      if (n < 0 && errno == EINTR) continue;
      break;
    }
    off += static_cast<std::size_t>(n);
  }
}

void WriteStr(int fd, const char *s) {
  WriteAll(fd, s, std::strlen(s));
}

// Render a non-negative int into buf as decimal, async-safe. Returns
// the number of chars written (no null terminator).
std::size_t IntToDec(int v, char *buf, std::size_t cap) {
  if (cap == 0) return 0;
  if (v == 0) {
    buf[0] = '0';
    return 1;
  }
  char tmp[16];
  std::size_t n = 0;
  unsigned uv = v < 0 ? static_cast<unsigned>(-v) : static_cast<unsigned>(v);
  while (uv > 0 && n < sizeof(tmp)) {
    tmp[n++] = static_cast<char>('0' + (uv % 10));
    uv /= 10;
  }
  std::size_t out = 0;
  if (v < 0 && out < cap) buf[out++] = '-';
  while (n > 0 && out < cap) buf[out++] = tmp[--n];
  return out;
}

void WriteInt(int fd, int v) {
  char buf[16];
  const std::size_t n = IntToDec(v, buf, sizeof(buf));
  WriteAll(fd, buf, n);
}

const char *SignalName(int sig) {
  switch (sig) {
    case SIGSEGV:
      return "SIGSEGV";
    case SIGABRT:
      return "SIGABRT";
    case SIGBUS:
      return "SIGBUS";
    case SIGILL:
      return "SIGILL";
    case SIGFPE:
      return "SIGFPE";
    default:
      return "signal";
  }
}

// The fault handler. Async-signal-safe throughout: it must not
// allocate, take a lock that a normal thread could hold, or call
// stdio. It writes a diagnostic then re-raises the signal under the
// default disposition so a core dump is still produced.
extern "C" void FaultHandler(int sig) {
  const int fd = STDERR_FILENO;
  WriteStr(fd, "\n=== einheit-ui FATAL ");
  WriteStr(fd, SignalName(sig));
  WriteStr(fd, " (");
  WriteInt(fd, sig);
  WriteStr(fd, ") ===\n");

  // In-flight request for this thread, if any was recorded.
  const std::size_t rlen = g_req_len;
  if (rlen > 0 && rlen < kReqBufSize) {
    WriteStr(fd, "in-flight request: ");
    WriteAll(fd, g_req_buf, rlen);
    WriteStr(fd, "\n");
  } else {
    WriteStr(fd, "in-flight request: (none on this thread)\n");
  }

  // Backtrace. backtrace()/backtrace_symbols_fd() are the async-safe
  // way to dump a stack from a handler.
  WriteStr(fd, "backtrace:\n");
  void *frames[64];
  const int n = ::backtrace(frames, 64);
  ::backtrace_symbols_fd(frames, n, fd);
  WriteStr(fd, "=== re-raising for core dump ===\n");

  // Restore the default handler and re-raise so the kernel dumps
  // core and the exit status reflects the signal.
  ::signal(sig, SIG_DFL);
  ::raise(sig);
}

extern "C" void StatusHandler(int) {
  g_status_pending = 1;
}
extern "C" void ReloadHandler(int) {
  g_reload_pending = 1;
}

void InstallOne(int sig, void (*handler)(int), int extra_flags) {
  struct sigaction sa;
  std::memset(&sa, 0, sizeof(sa));
  sa.sa_handler = handler;
  sigemptyset(&sa.sa_mask);
  // SA_RESTART so an interrupted read()/poll() resumes instead of
  // failing with EINTR across the whole codebase.
  sa.sa_flags = SA_RESTART | extra_flags;
  (void)::sigaction(sig, &sa, nullptr);
}

}  // namespace

auto SetCurrentRequest(std::string_view method, std::string_view target)
    -> void {
  // Compose "METHOD TARGET" into the fixed buffer, truncating to fit.
  // Publish the length last so a fault mid-write reports only the
  // bytes already stored.
  g_req_len = 0;
  std::size_t pos = 0;
  auto append = [&](std::string_view s) {
    for (char c : s) {
      if (pos + 1 >= kReqBufSize) break;
      g_req_buf[pos++] = c;
    }
  };
  append(method);
  if (pos + 1 < kReqBufSize) g_req_buf[pos++] = ' ';
  append(target);
  g_req_buf[pos] = '\0';
  g_req_len = pos;
}

auto ClearCurrentRequest() -> void {
  g_req_len = 0;
  g_req_buf[0] = '\0';
}

auto ConsumeStatusRequest() -> bool {
  if (g_status_pending) {
    g_status_pending = 0;
    return true;
  }
  return false;
}

auto ConsumeReloadRequest() -> bool {
  if (g_reload_pending) {
    g_reload_pending = 0;
    return true;
  }
  return false;
}

auto InstallSignalHandlers(const SignalConfig &cfg) -> void {
  if (cfg.ignore_sigpipe) {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    (void)::sigaction(SIGPIPE, &sa, nullptr);
  }

  if (cfg.fault_handlers) {
    // SA_NODEFER lets the re-raise deliver the same signal again under
    // the default handler instead of staying blocked.
    InstallOne(SIGSEGV, FaultHandler, SA_NODEFER);
    InstallOne(SIGABRT, FaultHandler, SA_NODEFER);
    InstallOne(SIGBUS, FaultHandler, SA_NODEFER);
    InstallOne(SIGILL, FaultHandler, SA_NODEFER);
    InstallOne(SIGFPE, FaultHandler, SA_NODEFER);
  }

  if (cfg.status_on_usr2) InstallOne(SIGUSR2, StatusHandler, 0);
  if (cfg.reload_on_hup) InstallOne(SIGHUP, ReloadHandler, 0);
}

}  // namespace einheit::ui
