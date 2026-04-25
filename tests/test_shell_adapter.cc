/// @file test_shell_adapter.cc
/// @brief Mount-time validation for the shell adapter. The PTY +
/// WebSocket bridge is intentionally not exercised here — it would
/// require spawning real launcher / cli binaries, which is what
/// the developer-mode smoke test in the README covers.
// Copyright (c) 2026 Einheit Networks

#include "einheit/adapters/shell/ui_adapter.h"

#include <crow.h>
#include <gtest/gtest.h>

#include "einheit/ui/render/template_engine.h"

namespace einheit::adapters::shell {
namespace {

auto MakeEngine() -> ui::render::TemplateEngine {
  ui::render::TemplateEngineConfig cfg;
  cfg.search_paths.push_back(TemplatesDir());
  return ui::render::TemplateEngine(std::move(cfg));
}

}  // namespace

TEST(ShellAdapterMount, RefusesWhenLauncherMissing) {
  ShellConfig cfg;
  cfg.launcher_path = "/nonexistent/einheit-shell-launcher";
  cfg.cli_path = "/usr/bin/true";  // exists, won't be spawned
  crow::SimpleApp app;
  auto eng = MakeEngine();
  auto r = Mount(app, eng, cfg);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ShellError::LauncherNotFound);
}

TEST(ShellAdapterMount, RefusesWhenCliMissing) {
  ShellConfig cfg;
  cfg.launcher_path = "/usr/bin/true";  // exists
  cfg.cli_path = "/nonexistent/einheit";
  crow::SimpleApp app;
  auto eng = MakeEngine();
  auto r = Mount(app, eng, cfg);
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, ShellError::CliNotFound);
}

TEST(ShellAdapterMount, AcceptsValidPaths) {
  ShellConfig cfg;
  cfg.launcher_path = "/usr/bin/true";
  cfg.cli_path = "/usr/bin/true";
  crow::SimpleApp app;
  auto eng = MakeEngine();
  // Mount only validates paths and registers routes — no
  // sub-process is spawned until a WebSocket actually connects,
  // so this round-trip is safe even with stand-in binaries.
  auto r = Mount(app, eng, cfg);
  ASSERT_TRUE(r.has_value()) << r.error().message;
}

}  // namespace einheit::adapters::shell
