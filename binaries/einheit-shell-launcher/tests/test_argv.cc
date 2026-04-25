/// @file test_argv.cc
/// @brief Coverage for the launcher's argv-shape helpers. The
/// hardening helpers themselves (seccomp, drop_privs, no_new_privs)
/// can't be exercised from inside a gtest harness without
/// permanently sandboxing the runner — they are integration-tested
/// implicitly by the einheit-ui shell adapter spawning a real
/// launcher process.
// Copyright (c) 2026 Einheit Networks

#include "binaries/einheit-shell-launcher/src/sandbox.h"

#include <algorithm>
#include <string>

#include <gtest/gtest.h>

namespace einheit::ui::launcher::sandbox {

TEST(BuildCliArgv, AlwaysPassesLockedFlag) {
  Args a;
  a.cli_path = "/usr/bin/einheit";
  auto v = BuildCliArgv(a);
  ASSERT_FALSE(v.empty());
  EXPECT_EQ(v.front(), "/usr/bin/einheit");
  EXPECT_NE(std::find(v.begin(), v.end(), "--locked"), v.end())
      << "--locked must always appear in the argv we exec";
}

TEST(BuildCliArgv, ForwardsAllNonEmptyOptionals) {
  Args a;
  a.cli_path = "/usr/bin/einheit";
  a.adapter = "hd-relay";
  a.role = "operator";
  a.target = "berlin-relay";
  a.endpoint = "ipc:///run/einheit/control.sock";
  a.event_endpoint = "ipc:///run/einheit/events.sock";
  auto v = BuildCliArgv(a);

  // Helper — find a flag and read its value.
  const auto value_of = [&](const std::string &flag) -> std::string {
    auto it = std::find(v.begin(), v.end(), flag);
    if (it == v.end() || std::next(it) == v.end()) return {};
    return *std::next(it);
  };
  EXPECT_EQ(value_of("--adapter"), "hd-relay");
  EXPECT_EQ(value_of("--role"), "operator");
  EXPECT_EQ(value_of("--target"), "berlin-relay");
  EXPECT_EQ(value_of("--endpoint"),
            "ipc:///run/einheit/control.sock");
  EXPECT_EQ(value_of("--event-endpoint"),
            "ipc:///run/einheit/events.sock");
}

TEST(BuildCliArgv, OmitsEmptyOptionals) {
  Args a;
  a.cli_path = "/usr/bin/einheit";
  a.role = "admin";
  // adapter, target, endpoint, event_endpoint left empty.
  auto v = BuildCliArgv(a);
  EXPECT_EQ(std::find(v.begin(), v.end(), "--adapter"), v.end());
  EXPECT_EQ(std::find(v.begin(), v.end(), "--target"), v.end());
  EXPECT_EQ(std::find(v.begin(), v.end(), "--endpoint"), v.end());
  EXPECT_EQ(std::find(v.begin(), v.end(), "--event-endpoint"),
            v.end());
}

}  // namespace einheit::ui::launcher::sandbox
