/// @file test_signals.cc
/// @brief The signal regime. SIGPIPE-ignore is the highest-value line
/// in the whole hardening effort — a client that drops mid-write must
/// not be able to kill the server — so it gets a behavioral test that
/// actually provokes the broken-pipe write. The control-signal flags
/// and the fault handler's in-flight-request report are covered too.
// Copyright (c) 2026 Einheit Networks

#include <gtest/gtest.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>

#include "einheit/ui/signals.h"

// Detect AddressSanitizer so we can skip the SEGV death test under it
// — ASan installs its own SIGSEGV handler and owns that disposition.
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define EINHEIT_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define EINHEIT_ASAN 1
#endif

namespace einheit::ui {
namespace {

TEST(Signals, SigpipeIsIgnoredSoBrokenPipeWriteDoesNotKillUs) {
  InstallSignalHandlers();  // ignore_sigpipe defaults true.

  int fds[2];
  ASSERT_EQ(::pipe(fds), 0);
  // Close the read end; any write to the write end now generates
  // SIGPIPE. With the default disposition this would terminate the
  // process; ignored, write() returns -1/EPIPE and we sail on.
  ::close(fds[0]);
  const ssize_t n = ::write(fds[1], "x", 1);
  EXPECT_EQ(n, -1);
  EXPECT_EQ(errno, EPIPE);
  ::close(fds[1]);
  // Reaching here at all is the assertion: we were not killed.
  SUCCEED();
}

TEST(Signals, Usr2SetsAndClearsStatusFlag) {
  InstallSignalHandlers();
  // Drain any stale flag first.
  (void)ConsumeStatusRequest();
  EXPECT_FALSE(ConsumeStatusRequest());
  ASSERT_EQ(::raise(SIGUSR2), 0);
  EXPECT_TRUE(ConsumeStatusRequest());
  // Consuming clears it.
  EXPECT_FALSE(ConsumeStatusRequest());
}

TEST(Signals, HupSetsAndClearsReloadFlag) {
  InstallSignalHandlers();
  (void)ConsumeReloadRequest();
  EXPECT_FALSE(ConsumeReloadRequest());
  ASSERT_EQ(::raise(SIGHUP), 0);
  EXPECT_TRUE(ConsumeReloadRequest());
  EXPECT_FALSE(ConsumeReloadRequest());
}

TEST(Signals, InstallIsIdempotent) {
  InstallSignalHandlers();
  InstallSignalHandlers();
  SUCCEED();
}

TEST(Signals, SetCurrentRequestTruncatesOversizeInputSafely) {
  // A pathologically long target must not overflow the fixed buffer.
  // We can't read the buffer back directly, but the call must return
  // cleanly and leave the process healthy.
  std::string huge(10000, 'a');
  SetCurrentRequest("GET", huge);
  ClearCurrentRequest();
  SUCCEED();
}

#ifndef EINHEIT_ASAN
using SignalsDeathTest = ::testing::Test;

TEST(SignalsDeathTest, FaultHandlerReportsRequestThenReRaises) {
  ::testing::GTEST_FLAG(death_test_style) = "threadsafe";
  EXPECT_EXIT(
      {
        InstallSignalHandlers();
        SetCurrentRequest("GET", "/runs/12/steps/3");
        ::raise(SIGSEGV);
      },
      ::testing::KilledBySignal(SIGSEGV),
      "in-flight request: GET /runs/12/steps/3");
}
#endif

}  // namespace
}  // namespace einheit::ui
