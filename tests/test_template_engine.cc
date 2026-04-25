/// @file test_template_engine.cc
/// @brief Inja wrapper tests: multi-root resolution, render success
/// and failure paths, hot-reload behaviour, autoescape.
// Copyright (c) 2026 Einheit Networks

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "einheit/ui/render/template_engine.h"

namespace einheit::ui::render {
namespace {

class TmpDir {
 public:
  TmpDir() {
    path_ = std::filesystem::temp_directory_path() /
            std::format("einheit_ui_tpl_{}_{}",
                        static_cast<int>(::getpid()), counter_++);
    std::filesystem::create_directories(path_);
  }
  ~TmpDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  TmpDir(const TmpDir &) = delete;
  auto operator=(const TmpDir &) -> TmpDir & = delete;

  auto Path() const -> std::string { return path_.string(); }

  auto WriteFile(const std::string &rel, const std::string &content)
      -> std::filesystem::path {
    auto full = path_ / rel;
    std::filesystem::create_directories(full.parent_path());
    std::ofstream f(full);
    f << content;
    return full;
  }

 private:
  static inline int counter_ = 0;
  std::filesystem::path path_;
};

}  // namespace

TEST(TemplateEngine, RendersSimpleInterpolation) {
  TmpDir d;
  d.WriteFile("hello.html.inja", "Hello {{ who }}");
  TemplateEngineConfig cfg;
  cfg.search_paths = {d.Path()};
  TemplateEngine eng(std::move(cfg));

  auto out = eng.Render("hello", {{"who", "world"}});
  ASSERT_TRUE(out.has_value()) << out.error().message;
  EXPECT_EQ(*out, "Hello world");
}

TEST(TemplateEngine, ResolvesAcrossSearchPathsFirstWins) {
  TmpDir a;
  TmpDir b;
  a.WriteFile("partials/card.html.inja", "from-A {{ x }}");
  b.WriteFile("partials/card.html.inja", "from-B {{ x }}");
  TemplateEngineConfig cfg;
  cfg.search_paths = {a.Path(), b.Path()};
  TemplateEngine eng(std::move(cfg));

  auto out = eng.Render("partials/card", {{"x", 1}});
  ASSERT_TRUE(out.has_value()) << out.error().message;
  EXPECT_NE(out->find("from-A"), std::string::npos);
  EXPECT_EQ(out->find("from-B"), std::string::npos);
}

TEST(TemplateEngine, FallsThroughToSecondPathWhenFirstMissing) {
  TmpDir a;
  TmpDir b;
  b.WriteFile("only_in_b.html.inja", "found");
  TemplateEngineConfig cfg;
  cfg.search_paths = {a.Path(), b.Path()};
  TemplateEngine eng(std::move(cfg));

  auto out = eng.Render("only_in_b", {});
  ASSERT_TRUE(out.has_value()) << out.error().message;
  EXPECT_EQ(*out, "found");
}

TEST(TemplateEngine, NotFoundWhenNoFileMatches) {
  TmpDir d;
  TemplateEngineConfig cfg;
  cfg.search_paths = {d.Path()};
  TemplateEngine eng(std::move(cfg));

  auto out = eng.Render("nope", {});
  ASSERT_FALSE(out.has_value());
  EXPECT_EQ(out.error().code, TemplateError::NotFound);
}

TEST(TemplateEngine, ParseFailedOnBrokenSyntax) {
  TmpDir d;
  // Unterminated `{%`: inja should reject this at parse time.
  d.WriteFile("broken.html.inja", "{% if x");
  TemplateEngineConfig cfg;
  cfg.search_paths = {d.Path()};
  TemplateEngine eng(std::move(cfg));

  auto out = eng.Render("broken", {});
  ASSERT_FALSE(out.has_value());
  // Either ParseFailed or RenderFailed depending on inja version;
  // the contract is "not silent success".
  EXPECT_NE(out.error().code, TemplateError::NotFound);
}

TEST(TemplateEngine, AutoescapesByDefault) {
  TmpDir d;
  d.WriteFile("xss.html.inja", "{{ payload }}");
  TemplateEngineConfig cfg;
  cfg.search_paths = {d.Path()};
  TemplateEngine eng(std::move(cfg));

  auto out =
      eng.Render("xss", {{"payload", "<script>alert(1)</script>"}});
  ASSERT_TRUE(out.has_value()) << out.error().message;
  EXPECT_EQ(out->find("<script>"), std::string::npos);
  EXPECT_NE(out->find("&lt;"), std::string::npos);
}

TEST(TemplateEngine, RenderStringInline) {
  TemplateEngineConfig cfg;
  TemplateEngine eng(std::move(cfg));
  auto out = eng.RenderString("x={{ x }}", {{"x", 42}});
  ASSERT_TRUE(out.has_value()) << out.error().message;
  EXPECT_EQ(*out, "x=42");
}

TEST(TemplateEngine, ResolvesNonHtmlTemplatesEndingInInja) {
  TmpDir d;
  d.WriteFile("theme.css.inja", ":root { --x: {{ x }}; }");
  TemplateEngineConfig cfg;
  cfg.search_paths = {d.Path()};
  TemplateEngine eng(std::move(cfg));

  auto out = eng.Render("theme.css", {{"x", "red"}});
  ASSERT_TRUE(out.has_value()) << out.error().message;
  EXPECT_NE(out->find("--x: red"), std::string::npos);
}

TEST(TemplateEngine, ResolveProducesAbsolutePath) {
  TmpDir d;
  d.WriteFile("foo.html.inja", "ok");
  TemplateEngineConfig cfg;
  cfg.search_paths = {d.Path()};
  TemplateEngine eng(std::move(cfg));

  auto p = eng.Resolve("foo");
  ASSERT_TRUE(p.has_value()) << p.error().message;
  EXPECT_TRUE(std::filesystem::path(*p).is_absolute());
  EXPECT_NE(p->find("foo.html.inja"), std::string::npos);
}

TEST(TemplateEngine, HotReloadPicksUpFileChanges) {
  TmpDir d;
  d.WriteFile("live.html.inja", "v1 {{ x }}");
  TemplateEngineConfig cfg;
  cfg.search_paths = {d.Path()};
  cfg.hot_reload = true;
  TemplateEngine eng(std::move(cfg));

  auto first = eng.Render("live", {{"x", 1}});
  ASSERT_TRUE(first.has_value()) << first.error().message;
  EXPECT_NE(first->find("v1"), std::string::npos);

  // Some filesystems have second-resolution mtimes. Wait briefly so
  // the rewrite is observable on every platform.
  std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  d.WriteFile("live.html.inja", "v2 {{ x }}");

  auto second = eng.Render("live", {{"x", 2}});
  ASSERT_TRUE(second.has_value()) << second.error().message;
  EXPECT_NE(second->find("v2"), std::string::npos);
}

}  // namespace einheit::ui::render
