/// @file test_partials.cc
/// @brief Render the framework's shared partials with realistic
/// contexts and assert the structural HTML they produce. Catches
/// breakage in partial templates without standing up an adapter.
// Copyright (c) 2026 Einheit Networks

#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "einheit/ui/render/template_engine.h"

namespace einheit::ui::render {
namespace {

auto MakeFrameworkEngine() -> TemplateEngine {
  TemplateEngineConfig cfg;
  cfg.search_paths = {EINHEIT_UI_FRAMEWORK_TEMPLATES_DIR};
  return TemplateEngine(std::move(cfg));
}

}  // namespace

TEST(PartialsCard, RendersTitleAndBody) {
  auto eng = MakeFrameworkEngine();
  nlohmann::json ctx = {
      {"title", "Status"},
      {"body", "uptime: 4d 12h"},
      {"semantic", "good"},
  };
  auto out = eng.Render("partials/card", ctx);
  ASSERT_TRUE(out.has_value()) << out.error().message;
  EXPECT_NE(out->find("Status"), std::string::npos);
  EXPECT_NE(out->find("uptime: 4d 12h"), std::string::npos);
  EXPECT_NE(out->find(R"(class="card card-good")"),
            std::string::npos);
}

TEST(PartialsTable, RendersHeadersAndCells) {
  auto eng = MakeFrameworkEngine();
  nlohmann::json ctx = {
      {"columns",
       nlohmann::json::array(
           {{{"header", "Peer"}, {"align", "left"}},
            {{"header", "State"}, {"align", "center"}}})},
      {"rows",
       nlohmann::json::array(
           {nlohmann::json::array(
               {{{"text", "munich"}},
                {{"text", "up"}, {"semantic", "good"}}})})},
  };
  auto out = eng.Render("partials/table", ctx);
  ASSERT_TRUE(out.has_value()) << out.error().message;
  EXPECT_NE(out->find("Peer"), std::string::npos);
  EXPECT_NE(out->find("State"), std::string::npos);
  EXPECT_NE(out->find("munich"), std::string::npos);
  EXPECT_NE(out->find("cell-good"), std::string::npos);
}

TEST(PartialsBadge, EmitsSemanticClass) {
  auto eng = MakeFrameworkEngine();
  auto out =
      eng.Render("partials/badge",
                 {{"text", "active"}, {"semantic", "good"}});
  ASSERT_TRUE(out.has_value()) << out.error().message;
  EXPECT_NE(out->find("active"), std::string::npos);
  EXPECT_NE(out->find("badge-good"), std::string::npos);
}

TEST(PartialsBadge, DefaultsToInfoSemantic) {
  auto eng = MakeFrameworkEngine();
  auto out = eng.Render("partials/badge", {{"text", "x"}});
  ASSERT_TRUE(out.has_value()) << out.error().message;
  EXPECT_NE(out->find("badge-info"), std::string::npos);
}

TEST(PartialsStatus, RendersLabelValuePairs) {
  auto eng = MakeFrameworkEngine();
  nlohmann::json ctx = {
      {"rows", nlohmann::json::array(
                   {{{"label", "hostname"}, {"value", "fw01"}},
                    {{"label", "uptime"},
                     {"value", "4d 12h"},
                     {"semantic", "good"}}})},
  };
  auto out = eng.Render("partials/status", ctx);
  ASSERT_TRUE(out.has_value()) << out.error().message;
  EXPECT_NE(out->find("hostname"), std::string::npos);
  EXPECT_NE(out->find("fw01"), std::string::npos);
  EXPECT_NE(out->find("uptime"), std::string::npos);
  EXPECT_NE(out->find("4d 12h"), std::string::npos);
  EXPECT_NE(out->find("cell-good"), std::string::npos);
}

TEST(PartialsButton, EmitsLabelAndDefaultPrimaryClass) {
  auto eng = MakeFrameworkEngine();
  // attrs must be an explicit empty object — `{{"attrs", {}}}`
  // initializer-list-collapses to JSON null, which inja can't
  // iterate.
  nlohmann::json ctx = {{"label", "Approve"}};
  ctx["attrs"] = nlohmann::json::object();
  auto out = eng.Render("partials/button", ctx);
  ASSERT_TRUE(out.has_value()) << out.error().message;
  EXPECT_NE(out->find("Approve"), std::string::npos);
  EXPECT_NE(out->find("btn-primary"), std::string::npos);
}

TEST(PartialsButton, ForwardsHtmxAttributes) {
  auto eng = MakeFrameworkEngine();
  nlohmann::json ctx = {
      {"label", "Deny"},
      {"semantic", "danger"},
      {"attrs",
       {{"hx-post", "/peers/abc/deny"},
        {"hx-confirm", "Sure?"}}},
  };
  auto out = eng.Render("partials/button", ctx);
  ASSERT_TRUE(out.has_value()) << out.error().message;
  EXPECT_NE(out->find("btn-danger"), std::string::npos);
  EXPECT_NE(out->find(R"(hx-post="/peers/abc/deny")"),
            std::string::npos);
  EXPECT_NE(out->find(R"(hx-confirm="Sure?")"), std::string::npos);
}

TEST(PartialsEmpty, UsesProvidedMessage) {
  auto eng = MakeFrameworkEngine();
  auto out = eng.Render("partials/empty",
                        {{"message", "no rules configured"}});
  ASSERT_TRUE(out.has_value()) << out.error().message;
  EXPECT_NE(out->find("no rules configured"), std::string::npos);
}

TEST(PartialsEmpty, FallsBackToDefaultMessage) {
  auto eng = MakeFrameworkEngine();
  auto out = eng.Render("partials/empty", nlohmann::json::object());
  ASSERT_TRUE(out.has_value()) << out.error().message;
  EXPECT_NE(out->find("nothing here yet"), std::string::npos);
}

TEST(PartialsLogEntry, RendersEntriesWithLowercasedLevelClass) {
  auto eng = MakeFrameworkEngine();
  nlohmann::json ctx = {
      {"entries",
       nlohmann::json::array(
           {{{"timestamp", "2026-04-25T10:30:00"},
             {"level", "INFO"},
             {"message", "engine started"}},
            {{"timestamp", "2026-04-25T10:30:05"},
             {"level", "ERROR"},
             {"message", "iface eth0 down"}}})},
  };
  auto out = eng.Render("partials/log_entry", ctx);
  ASSERT_TRUE(out.has_value()) << out.error().message;
  EXPECT_NE(out->find("engine started"), std::string::npos);
  EXPECT_NE(out->find("iface eth0 down"), std::string::npos);
  EXPECT_NE(out->find("log-info"), std::string::npos);
  EXPECT_NE(out->find("log-error"), std::string::npos);
}

TEST(PartialsLogEntry, FallsBackToEmptyMessageOnNoEntries) {
  auto eng = MakeFrameworkEngine();
  auto out = eng.Render("partials/log_entry",
                        {{"entries", nlohmann::json::array()}});
  ASSERT_TRUE(out.has_value()) << out.error().message;
  EXPECT_NE(out->find("no log entries yet"), std::string::npos);
}

TEST(PartialsError, RendersCodeMessageHint) {
  auto eng = MakeFrameworkEngine();
  nlohmann::json ctx = {{"code", "render_failed"},
                        {"message", "template missing"},
                        {"hint", "check the path"}};
  auto out = eng.Render("partials/error", ctx);
  ASSERT_TRUE(out.has_value()) << out.error().message;
  EXPECT_NE(out->find("render_failed"), std::string::npos);
  EXPECT_NE(out->find("template missing"), std::string::npos);
  EXPECT_NE(out->find("check the path"), std::string::npos);
}

}  // namespace einheit::ui::render
