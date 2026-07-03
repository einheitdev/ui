/// @file test_confd_adapter.cc
/// @brief Contract checks on the confd adapter: factory / slug / nav,
/// and — the part that matters for the management-plane spec — that
/// its templates actually surface the confd lifecycle: the mutation
/// controls that POST to the engine routes, and the commit-confirmed
/// countdown + confirm control a reconnecting operator must see.
// Copyright (c) 2026 Einheit Networks

#include <regex>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "einheit/adapters/confd/ui_adapter.h"
#include "einheit/ui/adapter.h"
#include "einheit/ui/render/template_engine.h"

namespace einheit::adapters::confd {
namespace {

auto MakeEngine(const std::string &templates_dir)
    -> ui::render::TemplateEngine {
  ui::render::TemplateEngineConfig cfg;
  cfg.search_paths = {templates_dir, EINHEIT_UI_FRAMEWORK_TEMPLATES_DIR};
  return ui::render::TemplateEngine(std::move(cfg));
}

// A status context shaped exactly like ConfdUiAdapter::StatusJson.
auto StatusCtx(bool armed, bool in_configure) -> nlohmann::json {
  nlohmann::json pend =
      armed ? nlohmann::json{{"armed", true},
                             {"seconds", 192},
                             {"mmss", "3m12s"},
                             {"commit", 4},
                             {"rollback_to", 3}}
            : nlohmann::json{{"armed", false}, {"seconds", 0},
                             {"mmss", "0m00s"}, {"commit", 0},
                             {"rollback_to", 0}};
  return {
      {"in_configure", in_configure},
      {"session_id", "confd-1"},
      {"running", {{{"key", "device.hostname"}, {"value", "sw1"}}}},
      {"running_count", 1},
      {"pending", std::move(pend)},
      {"audit",
       {{{"user", "alice"}, {"role", "operator"},
         {"command", "commit confirmed"}, {"outcome", "ok"}, {"ok", true}}}},
      {"message", ""}};
}

TEST(ConfdAdapter, FactoryAndSlug) {
  auto a = NewConfdUiAdapter();
  ASSERT_NE(a, nullptr);
  static const std::regex kSlug(R"(^[a-z][a-z0-9-]*$)");
  EXPECT_TRUE(std::regex_match(a->Slug(), kSlug)) << a->Slug();
  bool has_config = false;
  for (const auto &n : a->Nav()) {
    if (n.slug == "config") has_config = true;
  }
  EXPECT_TRUE(has_config);
}

TEST(ConfdAdapter, PageSurfacesEngineMutationControls) {
  auto a = NewConfdUiAdapter();
  auto eng = MakeEngine(a->TemplatesDir());
  auto page = eng.Render("confd/config", StatusCtx(false, false));
  ASSERT_TRUE(page.has_value()) << page.error().message;
  // Each mutation control POSTs to an engine-driven route — none of
  // them talks to a daemon directly.
  for (const char *route : {"/config/configure", "/config/set",
                            "/config/delete", "/config/commit",
                            "/config/commit-confirmed",
                            "/config/rollback"}) {
    EXPECT_NE(page->find(route), std::string::npos) << route;
  }
}

TEST(ConfdAdapter, CountdownAndConfirmSurfacedWhenArmed) {
  auto a = NewConfdUiAdapter();
  auto eng = MakeEngine(a->TemplatesDir());
  auto armed = eng.Render("confd/status", StatusCtx(true, false));
  ASSERT_TRUE(armed.has_value()) << armed.error().message;
  // The reconnecting operator sees the live countdown and a confirm
  // control wired to the engine's confirm route.
  EXPECT_NE(armed->find("3m12s"), std::string::npos);
  EXPECT_NE(armed->find("automatic rollback"), std::string::npos);
  EXPECT_NE(armed->find("/config/confirm"), std::string::npos);

  // When nothing is pending, no countdown is shown.
  auto idle = eng.Render("confd/status", StatusCtx(false, false));
  ASSERT_TRUE(idle.has_value()) << idle.error().message;
  EXPECT_EQ(idle->find("automatic rollback"), std::string::npos);
}

TEST(ConfdAdapter, StatusShowsRunningConfigAndAudit) {
  auto a = NewConfdUiAdapter();
  auto eng = MakeEngine(a->TemplatesDir());
  auto out = eng.Render("confd/status", StatusCtx(false, true));
  ASSERT_TRUE(out.has_value()) << out.error().message;
  // Running config (direct read) and the mutation audit trail render.
  EXPECT_NE(out->find("device.hostname"), std::string::npos);
  EXPECT_NE(out->find("sw1"), std::string::npos);
  EXPECT_NE(out->find("commit confirmed"), std::string::npos);
  EXPECT_NE(out->find("candidate open"), std::string::npos);
}

}  // namespace
}  // namespace einheit::adapters::confd
