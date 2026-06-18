/// @file test_takt_adapter.cc
/// @brief Adapter contract tests for the takt UI adapter.
/// Checks slug/nav/template rendering against fake JSON
/// shaped like the takt REST API responses.
// Copyright (c) 2026 Einheit Networks

#include <regex>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "einheit/adapters/takt/takt_client.h"
#include "einheit/adapters/takt/ui_adapter.h"
#include "einheit/ui/adapter.h"
#include "einheit/ui/render/template_engine.h"

namespace einheit::adapters::takt {

TEST(TaktAdapter, FactoryReturnsAdapter) {
  TaktClientConfig cfg;
  cfg.base_url = "http://127.0.0.1:1";
  auto a = NewTaktUiAdapter(std::move(cfg));
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->Slug(), "takt");
}

TEST(TaktAdapter, SlugMatchesContract) {
  TaktClientConfig cfg;
  auto a = NewTaktUiAdapter(std::move(cfg));
  static const std::regex kSlug(
      R"(^[a-z][a-z0-9-]*$)");
  EXPECT_TRUE(std::regex_match(a->Slug(), kSlug))
      << a->Slug();
}

TEST(TaktAdapter, DisplayNameIsTakt) {
  TaktClientConfig cfg;
  auto a = NewTaktUiAdapter(std::move(cfg));
  EXPECT_EQ(a->DisplayName(), "takt");
}

TEST(TaktAdapter, NavCoversAllPages) {
  TaktClientConfig cfg;
  auto a = NewTaktUiAdapter(std::move(cfg));
  std::vector<std::string> slugs;
  for (const auto &n : a->Nav()) {
    slugs.push_back(n.slug);
  }
  for (const auto *expected : {
           "dashboard", "workspaces", "pipeline",
           "agents", "targets", "runs", "settings"}) {
    EXPECT_NE(
        std::find(slugs.begin(), slugs.end(), expected),
        slugs.end())
        << "nav missing slug: " << expected;
  }
}

TEST(TaktAdapter, NavEntriesHaveIcons) {
  TaktClientConfig cfg;
  auto a = NewTaktUiAdapter(std::move(cfg));
  for (const auto &n : a->Nav()) {
    EXPECT_FALSE(n.icon.empty())
        << "nav entry '" << n.slug
        << "' missing icon";
  }
}

TEST(TaktAdapter,
     TemplatesResolveAndRenderWithFakeJson) {
  TaktClientConfig cfg;
  auto a = NewTaktUiAdapter(std::move(cfg));
  ui::render::TemplateEngineConfig tcfg;
  tcfg.search_paths = {
      a->TemplatesDir(),
      EINHEIT_UI_FRAMEWORK_TEMPLATES_DIR};
  ui::render::TemplateEngine eng(std::move(tcfg));

  // Dashboard
  nlohmann::json dashboard = {
      {"summary",
       nlohmann::json::array(
           {{{"label", "workspaces"}, {"value", "3"}},
            {{"label", "targets"}, {"value", "5"}},
            {{"label", "active runs"},
             {"value", "1"},
             {"semantic", "good"}},
            {{"label", "agents"}, {"value", "2"}}})},
      {"workspaces",
       nlohmann::json::array(
           {{{"name", "feature-auth"},
             {"branch", "feature-auth"},
             {"repo_count", 3},
             {"repos", "cli, ui, takt"}}})},
      {"runs",
       nlohmann::json::array(
           {{{"id", 42},
             {"workspace", "feature-auth"},
             {"status", "passed"},
             {"status_semantic", "good"},
             {"trigger", "manual"},
             {"created_at", "2026-06-18T10:00:00"}}})},
      {"targets",
       nlohmann::json::array(
           {{{"name", "deb-02"},
             {"type", "vm"},
             {"host", "10.101.0.2"},
             {"template", false},
             {"claimed_by", "feature-auth"},
             {"state_semantic", "warn"}}})},
      {"agents", nlohmann::json::array()},
  };
  auto db = eng.Render("takt/dashboard", dashboard);
  ASSERT_TRUE(db.has_value()) << db.error().message;
  EXPECT_NE(db->find("feature-auth"), std::string::npos);
  EXPECT_NE(db->find("deb-02"), std::string::npos);

  // Dashboard summary (WebSocket fragment)
  nlohmann::json summary_data = {
      {"summary",
       nlohmann::json::array(
           {{{"label", "workspaces"}, {"value", "3"}},
            {{"label", "targets"}, {"value", "5"}}})}};
  auto ds = eng.Render(
      "takt/dashboard_summary", summary_data);
  ASSERT_TRUE(ds.has_value()) << ds.error().message;
  EXPECT_NE(ds->find("overview"), std::string::npos);

  // Workspaces
  nlohmann::json ws_data = {
      {"workspaces",
       nlohmann::json::array(
           {{{"name", "test-ws"},
             {"branch", "test-ws"},
             {"repos", "cli, ui"},
             {"repo_count", 2}}})}};
  auto ws = eng.Render("takt/workspaces", ws_data);
  ASSERT_TRUE(ws.has_value()) << ws.error().message;
  EXPECT_NE(ws->find("test-ws"), std::string::npos);

  // Workspaces empty
  auto ws_empty = eng.Render(
      "takt/workspaces",
      {{"workspaces", nlohmann::json::array()}});
  ASSERT_TRUE(ws_empty.has_value())
      << ws_empty.error().message;
  EXPECT_NE(
      ws_empty->find("no workspaces"),
      std::string::npos);

  // Targets
  nlohmann::json tgt_data = {
      {"targets",
       nlohmann::json::array(
           {{{"name", "deb-02"},
             {"type", "vm"},
             {"host", "10.101.0.2"},
             {"template", false},
             {"claimed_by", "test-ws"},
             {"state_semantic", "warn"}}})}};
  auto tgt = eng.Render("takt/targets", tgt_data);
  ASSERT_TRUE(tgt.has_value()) << tgt.error().message;
  EXPECT_NE(tgt->find("deb-02"), std::string::npos);
  EXPECT_NE(tgt->find("release"), std::string::npos);

  // Runs
  nlohmann::json runs_data = {
      {"runs",
       nlohmann::json::array(
           {{{"id", 1},
             {"workspace", "test-ws"},
             {"status", "running"},
             {"status_semantic", "good"},
             {"trigger", "push"},
             {"created_at",
              "2026-06-18T12:00:00"}}})}};
  auto ru = eng.Render("takt/runs", runs_data);
  ASSERT_TRUE(ru.has_value()) << ru.error().message;
  EXPECT_NE(ru->find("test-ws"), std::string::npos);
  EXPECT_NE(
      ru->find("running"), std::string::npos);

  // Runs table (WebSocket fragment)
  auto rt = eng.Render("takt/runs_table", runs_data);
  ASSERT_TRUE(rt.has_value()) << rt.error().message;
  EXPECT_NE(
      rt->find("running"), std::string::npos);

  // Agents
  nlohmann::json agents_data = {
      {"agents",
       nlohmann::json::array(
           {{{"workspace", "test-ws"},
             {"name", "test"},
             {"status", "running"},
             {"status_semantic", "good"},
             {"model", "sonnet"},
             {"num_turns", 5},
             {"cost_usd", 0.12}}})}};
  auto ag = eng.Render("takt/agents", agents_data);
  ASSERT_TRUE(ag.has_value()) << ag.error().message;
  EXPECT_NE(ag->find("sonnet"), std::string::npos);

  // Agents table fragment (WebSocket swap target)
  auto at = eng.Render("takt/agents_table", agents_data);
  ASSERT_TRUE(at.has_value()) << at.error().message;
  EXPECT_NE(at->find("sonnet"), std::string::npos);

  // Agents empty
  auto ag_empty = eng.Render(
      "takt/agents",
      {{"agents", nlohmann::json::array()}});
  ASSERT_TRUE(ag_empty.has_value())
      << ag_empty.error().message;
  EXPECT_NE(
      ag_empty->find("no active agents"),
      std::string::npos);

  // Pipeline
  nlohmann::json pl_data = {
      {"pipelines",
       nlohmann::json::array(
           {{{"workspace", "test-ws"},
             {"steps",
              nlohmann::json::array(
                  {{{"seq", 1},
                    {"name", "test"},
                    {"step_type", "agent"},
                    {"timeout_secs", 1800}}})}}})}};
  auto pl = eng.Render("takt/pipeline", pl_data);
  ASSERT_TRUE(pl.has_value()) << pl.error().message;
  EXPECT_NE(pl->find("test-ws"), std::string::npos);
  EXPECT_NE(pl->find("1800s"), std::string::npos);
}

TEST(TaktAdapter, WorkspaceDetailTemplateRenders) {
  TaktClientConfig cfg;
  auto a = NewTaktUiAdapter(std::move(cfg));
  ui::render::TemplateEngineConfig tcfg;
  tcfg.search_paths = {
      a->TemplatesDir(),
      EINHEIT_UI_FRAMEWORK_TEMPLATES_DIR};
  ui::render::TemplateEngine eng(std::move(tcfg));
  nlohmann::json data = {
      {"name", "test-ws"},
      {"repos",
       nlohmann::json::array(
           {{{"repo", "cli"},
             {"branch", "test-ws"},
             {"status", "clean"},
             {"status_semantic", "good"}},
            {{"repo", "ui"},
             {"branch", "test-ws"},
             {"status", "2 changed files"},
             {"status_semantic", "warn"}}})}};
  auto r = eng.Render("takt/workspace_detail", data);
  ASSERT_TRUE(r.has_value()) << r.error().message;
  EXPECT_NE(r->find("test-ws"), std::string::npos);
  EXPECT_NE(r->find("clean"), std::string::npos);
  EXPECT_NE(r->find("2 changed files"),
            std::string::npos);
}

TEST(TaktAdapter, StepOutputTemplateRenders) {
  TaktClientConfig cfg;
  auto a = NewTaktUiAdapter(std::move(cfg));
  ui::render::TemplateEngineConfig tcfg;
  tcfg.search_paths = {
      a->TemplatesDir(),
      EINHEIT_UI_FRAMEWORK_TEMPLATES_DIR};
  ui::render::TemplateEngine eng(std::move(tcfg));
  nlohmann::json data = {
      {"step",
       {{"name", "test"},
        {"status", "completed"},
        {"step_type", "agent"},
        {"cost_usd", 0.42}}},
      {"run_id", 1},
      {"entries",
       nlohmann::json::array(
           {{{"timestamp", "2026-06-18T10:00:00"},
             {"level", "INFO"},
             {"message", "running tests..."}}})}};
  auto r = eng.Render("takt/step_output", data);
  ASSERT_TRUE(r.has_value()) << r.error().message;
  EXPECT_NE(r->find("running tests"),
            std::string::npos);
  EXPECT_NE(r->find("$0.42"), std::string::npos);
}

TEST(TaktClient, BadUrlSurfacesAtFirstRequest) {
  TaktClientConfig cfg;
  cfg.base_url = "no-scheme";
  TaktClient c(std::move(cfg));
  auto r = c.Get("/api/workspaces");
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, TaktClientError::BadUrl);
}

}  // namespace einheit::adapters::takt
