/// @file test_example_adapter.cc
/// @brief Sanity checks on the shipped example adapter — its
/// templates resolve, its nav surfaces the routes the dashboard
/// links to, and its slug shape matches the contract.
// Copyright (c) 2026 Einheit Networks

#include <regex>
#include <string>

#include <gtest/gtest.h>

#include "einheit/adapters/example/ui_adapter.h"
#include "einheit/ui/adapter.h"
#include "einheit/ui/render/template_engine.h"

namespace einheit::adapters::example {

TEST(ExampleAdapter, FactoryReturnsAdapter) {
  auto a = NewExampleUiAdapter();
  ASSERT_NE(a, nullptr);
}

TEST(ExampleAdapter, SlugMatchesContract) {
  auto a = NewExampleUiAdapter();
  // Contract from adapter.h: ^[a-z][a-z0-9-]*$
  static const std::regex kSlug(R"(^[a-z][a-z0-9-]*$)");
  EXPECT_TRUE(std::regex_match(a->Slug(), kSlug)) << a->Slug();
}

TEST(ExampleAdapter, NavCoversDashboardAndCounters) {
  auto a = NewExampleUiAdapter();
  bool dashboard = false;
  bool counters = false;
  for (const auto &n : a->Nav()) {
    if (n.slug == "dashboard") dashboard = true;
    if (n.slug == "counters") counters = true;
  }
  EXPECT_TRUE(dashboard);
  EXPECT_TRUE(counters);
}

TEST(ExampleAdapter, TemplatesResolveAgainstAdapterDir) {
  auto a = NewExampleUiAdapter();
  ui::render::TemplateEngineConfig cfg;
  // Match the production setup: adapter dir first (so it shadows
  // framework partials by name), framework dir second so any
  // `{% include "partials/..." %}` in the adapter resolves.
  cfg.search_paths = {a->TemplatesDir(),
                      EINHEIT_UI_FRAMEWORK_TEMPLATES_DIR};
  ui::render::TemplateEngine eng(std::move(cfg));

  auto card = eng.Render("example/counter_card", {{"counter", 0}});
  ASSERT_TRUE(card.has_value()) << card.error().message;
  EXPECT_NE(card->find("counter-card"), std::string::npos);

  auto dash = eng.Render("example/dashboard", {{"counter", 0}});
  ASSERT_TRUE(dash.has_value()) << dash.error().message;
  EXPECT_NE(dash->find("tick"), std::string::npos);
}

TEST(ExampleAdapter, CounterTemplateRendersValueIntoBody) {
  auto a = NewExampleUiAdapter();
  ui::render::TemplateEngineConfig cfg;
  // Match the production setup: adapter dir first (so it shadows
  // framework partials by name), framework dir second so any
  // `{% include "partials/..." %}` in the adapter resolves.
  cfg.search_paths = {a->TemplatesDir(),
                      EINHEIT_UI_FRAMEWORK_TEMPLATES_DIR};
  ui::render::TemplateEngine eng(std::move(cfg));
  auto out = eng.Render("example/counter_card", {{"counter", 42}});
  ASSERT_TRUE(out.has_value()) << out.error().message;
  EXPECT_NE(out->find("42"), std::string::npos);
}

}  // namespace einheit::adapters::example
