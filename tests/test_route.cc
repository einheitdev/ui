/// @file test_route.cc
/// @brief Format detection + Render() shape tests. Constructs Crow
/// requests by hand (Crow exposes a default-constructible request);
/// no real server stands up.
// Copyright (c) 2026 Einheit Networks

#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <system_error>

#include <crow.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "einheit/ui/render/template_engine.h"
#include "einheit/ui/route.h"

namespace einheit::ui {
namespace {

class TmpDir {
 public:
  TmpDir() {
    path_ = std::filesystem::temp_directory_path() /
            std::format("einheit_ui_route_{}_{}",
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
      -> void {
    auto full = std::filesystem::path(path_) / rel;
    std::filesystem::create_directories(full.parent_path());
    std::ofstream f(full);
    f << content;
  }

 private:
  static inline int counter_ = 0;
  std::filesystem::path path_;
};

auto MakeReq(std::initializer_list<
                 std::pair<std::string, std::string>> headers,
             std::string query = "") -> crow::request {
  crow::request r;
  for (auto &h : headers) r.add_header(h.first, h.second);
  if (!query.empty()) {
    r.url_params = crow::query_string{"?" + query};
  }
  return r;
}

auto MakeEngine(const TmpDir &d) -> render::TemplateEngine {
  render::TemplateEngineConfig cfg;
  cfg.search_paths = {d.Path()};
  return render::TemplateEngine(std::move(cfg));
}

}  // namespace

TEST(IsHxRequest, TrueWhenHeaderMatches) {
  auto r = MakeReq({{"HX-Request", "true"}});
  EXPECT_TRUE(IsHxRequest(r));
}

TEST(IsHxRequest, FalseWhenHeaderMissing) {
  auto r = MakeReq({});
  EXPECT_FALSE(IsHxRequest(r));
}

TEST(IsHxRequest, FalseWhenHeaderNotTrue) {
  auto r = MakeReq({{"HX-Request", "false"}});
  EXPECT_FALSE(IsHxRequest(r));
}

TEST(DetectFormat, DefaultIsPage) {
  auto r = MakeReq({});
  EXPECT_EQ(DetectFormat(r), ResponseFormat::Page);
}

TEST(DetectFormat, HxRequestSelectsFragment) {
  auto r = MakeReq({{"HX-Request", "true"}});
  EXPECT_EQ(DetectFormat(r), ResponseFormat::Fragment);
}

TEST(DetectFormat, AcceptJsonSelectsJson) {
  auto r = MakeReq({{"Accept", "application/json"}});
  EXPECT_EQ(DetectFormat(r), ResponseFormat::Json);
}

TEST(DetectFormat, ExplicitQueryWinsOverHx) {
  auto r = MakeReq({{"HX-Request", "true"}}, "format=json");
  EXPECT_EQ(DetectFormat(r), ResponseFormat::Json);
}

TEST(DetectFormat, ExplicitQueryHtmlOverridesAcceptJson) {
  auto r = MakeReq({{"Accept", "application/json"}}, "format=html");
  EXPECT_EQ(DetectFormat(r), ResponseFormat::Page);
}

TEST(Render, JsonReturnsDataDumpAndJsonContentType) {
  TmpDir d;
  auto eng = MakeEngine(d);
  RenderArgs args;
  args.fragment = "unused";
  args.data = {{"k", "v"}, {"n", 42}};
  auto resp = Render(eng, ResponseFormat::Json, args);
  ASSERT_TRUE(resp.has_value()) << resp.error().message;
  EXPECT_EQ(resp->code, 200);
  EXPECT_NE(resp->body.find("\"k\""), std::string::npos);
  EXPECT_NE(resp->body.find("\"v\""), std::string::npos);
  EXPECT_NE(resp->body.find("\"n\":42"), std::string::npos);
}

TEST(Render, FragmentReturnsRenderedTemplateOnly) {
  TmpDir d;
  d.WriteFile("greet.html.inja", "<p>hi {{ who }}</p>");
  auto eng = MakeEngine(d);
  RenderArgs args;
  args.fragment = "greet";
  args.data = {{"who", "alice"}};
  auto resp = Render(eng, ResponseFormat::Fragment, args);
  ASSERT_TRUE(resp.has_value()) << resp.error().message;
  EXPECT_EQ(resp->body, "<p>hi alice</p>");
}

TEST(Render, PageSplicesFragmentIntoLayoutMarker) {
  TmpDir d;
  d.WriteFile("inner.html.inja", "INNER {{ x }}");
  // The framework splices the rendered inner fragment into the
  // literal `<!--EINHEIT_BODY-->` placeholder. meta.* still flows
  // through inja interpolation so it gets autoescape protection.
  d.WriteFile("layout.html.inja",
              "BEFORE\n<!--EINHEIT_BODY-->\nAFTER {{ meta.title }}");
  auto eng = MakeEngine(d);
  RenderArgs args;
  args.fragment = "inner";
  args.layout = "layout";
  args.data = {{"x", "core"}};
  args.meta = {{"title", "page"}};
  auto resp = Render(eng, ResponseFormat::Page, args);
  ASSERT_TRUE(resp.has_value()) << resp.error().message;
  EXPECT_NE(resp->body.find("BEFORE"), std::string::npos);
  EXPECT_NE(resp->body.find("INNER core"), std::string::npos);
  EXPECT_NE(resp->body.find("AFTER page"), std::string::npos);
  // Marker must be consumed.
  EXPECT_EQ(resp->body.find("<!--EINHEIT_BODY-->"),
            std::string::npos);
}

TEST(Render, PageBackfillsMetaSoLayoutDoesNotErrorOnMissingKeys) {
  TmpDir d;
  d.WriteFile("inner.html.inja", "INNER");
  // Layout iterates meta.nav and reads meta.title — both keys are
  // entirely absent in the args.meta we pass in. The framework
  // must default them so inja doesn't raise variable-not-found.
  d.WriteFile(
      "layout.html.inja",
      "T={{ meta.title }};N=[{% for e in meta.nav %}{{ e.label }}{% endfor %}];"
      "<!--EINHEIT_BODY-->");
  auto eng = MakeEngine(d);
  RenderArgs args;
  args.fragment = "inner";
  args.layout = "layout";
  // args.meta intentionally left empty — backfill should kick in.
  auto resp = Render(eng, ResponseFormat::Page, args);
  ASSERT_TRUE(resp.has_value()) << resp.error().message;
  EXPECT_NE(resp->body.find("T=einheit"), std::string::npos);
  EXPECT_NE(resp->body.find("N=[]"), std::string::npos);
  EXPECT_NE(resp->body.find("INNER"), std::string::npos);
}

TEST(Render, MissingFragmentSurfacesTemplateError) {
  TmpDir d;
  auto eng = MakeEngine(d);
  RenderArgs args;
  args.fragment = "does/not/exist";
  auto resp = Render(eng, ResponseFormat::Fragment, args);
  ASSERT_FALSE(resp.has_value());
  EXPECT_EQ(resp.error().code, RouteError::TemplateFailed);
}

TEST(Render, OverloadHonoursIncomingHxHeader) {
  TmpDir d;
  d.WriteFile("inner.html.inja", "INNER");
  d.WriteFile("layout.html.inja", "WRAPPED:<!--EINHEIT_BODY-->");
  auto eng = MakeEngine(d);

  RenderArgs args;
  args.fragment = "inner";
  args.layout = "layout";

  auto hx = MakeReq({{"HX-Request", "true"}});
  auto frag = Render(eng, hx, args);
  ASSERT_TRUE(frag.has_value()) << frag.error().message;
  EXPECT_EQ(frag->body, "INNER");

  auto plain = MakeReq({});
  auto page = Render(eng, plain, args);
  ASSERT_TRUE(page.has_value()) << page.error().message;
  EXPECT_NE(page->body.find("WRAPPED:"), std::string::npos);
}

TEST(RenderError, JsonShapeContainsCodeMessageHint) {
  TmpDir d;
  auto eng = MakeEngine(d);
  auto r = MakeReq({{"Accept", "application/json"}});
  auto resp =
      RenderError(eng, r, 404, "not_found", "missing", "try /home");
  EXPECT_EQ(resp.code, 404);
  EXPECT_NE(resp.body.find("\"code\""), std::string::npos);
  EXPECT_NE(resp.body.find("not_found"), std::string::npos);
  EXPECT_NE(resp.body.find("missing"), std::string::npos);
  EXPECT_NE(resp.body.find("try /home"), std::string::npos);
}

TEST(RenderError, HtmlFallbackWhenErrorTemplateMissing) {
  TmpDir d;  // no error template installed.
  auto eng = MakeEngine(d);
  auto r = MakeReq({});
  auto resp = RenderError(eng, r, 500, "boom", "thing exploded");
  EXPECT_EQ(resp.code, 500);
  EXPECT_NE(resp.body.find("boom"), std::string::npos);
  EXPECT_NE(resp.body.find("thing exploded"), std::string::npos);
}

}  // namespace einheit::ui
