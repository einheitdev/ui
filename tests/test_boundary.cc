/// @file test_boundary.cc
/// @brief Guard() is the framework's exception-containment primitive.
/// Every hardened non-route boundary (WebSocket callbacks, sampler
/// threads, the PTY reader) routes through it, so its contract —
/// never propagate, report success/failure honestly — is load-bearing.
// Copyright (c) 2026 Einheit Networks

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "einheit/ui/boundary.h"

namespace einheit::ui {
namespace {

TEST(Guard, ReturnsTrueWhenCallableSucceeds) {
  bool ran = false;
  const bool ok = Guard("test", [&] { ran = true; });
  EXPECT_TRUE(ok);
  EXPECT_TRUE(ran);
}

TEST(Guard, ReturnsFalseAndSwallowsStdException) {
  const bool ok = Guard("test", [] { throw std::runtime_error("boom"); });
  EXPECT_FALSE(ok);
}

TEST(Guard, ReturnsFalseAndSwallowsNonStdException) {
  const bool ok = Guard("test", [] { throw 42; });
  EXPECT_FALSE(ok);
}

TEST(Guard, DoesNotPropagateSoCallerContinues) {
  // The whole point: control returns to the line after Guard even
  // when the callable throws. If Guard rethrew, we'd never reach the
  // assertion.
  int reached = 0;
  Guard("first", [] { throw std::logic_error("x"); });
  reached = 1;
  Guard("second", [&] { reached = 2; });
  EXPECT_EQ(reached, 2);
}

TEST(Guard, RunsSideEffectsBeforeAThrow) {
  int counter = 0;
  const bool ok = Guard("test", [&] {
    ++counter;
    throw std::runtime_error("after increment");
  });
  EXPECT_FALSE(ok);
  EXPECT_EQ(counter, 1);
}

// This is the sampler/poller contract: a loop body that throws every
// iteration must keep looping, not die. Mirrors how the hd-relay
// sampler and takt poller wrap each iteration in Guard so a bad
// sample never kills the background thread (or the process).
TEST(Guard, GuardedLoopKeepsRunningAfterEveryThrow) {
  int iterations = 0;
  for (int i = 0; i < 5; ++i) {
    Guard("sampler-like", [&] {
      ++iterations;
      throw std::runtime_error("bad sample");
    });
  }
  EXPECT_EQ(iterations, 5);
}

}  // namespace
}  // namespace einheit::ui
