/// @file test_stream.cc
/// @brief OOB-wrapping helper + EventStream Bind/Publish path. The
/// WebSocket plumbing (Mount, OnOpen, OnClose) needs a real Crow
/// app + a TCP loopback to exercise; that's the integration tier.
/// This file targets what's pure.
// Copyright (c) 2026 Einheit Networks

#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <system_error>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "einheit/ui/render/template_engine.h"
#include "einheit/ui/stream.h"

namespace einheit::ui {
namespace {

class TmpDir {
 public:
  TmpDir() {
    path_ = std::filesystem::temp_directory_path() /
            std::format("einheit_ui_stream_{}_{}",
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

auto MakeBinding(std::string topic, std::string fragment,
                 std::string target,
                 std::string strategy = "outerHTML")
    -> TopicBinding {
  TopicBinding b;
  b.topic = std::move(topic);
  b.fragment = std::move(fragment);
  b.swap_target = std::move(target);
  b.swap_strategy = std::move(strategy);
  return b;
}

}  // namespace

TEST(BuildOobWrapper, WrapsBodyWithHxSwapOob) {
  const auto out =
      BuildOobWrapper("<span>v1</span>",
                      MakeBinding("t", "f", "counter-card"));
  EXPECT_NE(out.find(R"(hx-swap-oob="outerHTML:#counter-card")"),
            std::string::npos);
  EXPECT_NE(out.find("<span>v1</span>"), std::string::npos);
}

TEST(BuildOobWrapper, HonoursStrategyOverride) {
  const auto out = BuildOobWrapper(
      "<li>x</li>",
      MakeBinding("t", "f", "list", "beforeend"));
  EXPECT_NE(out.find(R"(hx-swap-oob="beforeend:#list")"),
            std::string::npos);
}

TEST(BuildOobWrapper, RendersCleanlyWhenBodyEmpty) {
  const auto out =
      BuildOobWrapper("", MakeBinding("t", "f", "anchor"));
  EXPECT_NE(out.find(R"(hx-swap-oob="outerHTML:#anchor")"),
            std::string::npos);
  EXPECT_NE(out.find("></div>"), std::string::npos);
}

TEST(EventStream, PublishUnknownTopicReturnsError) {
  TmpDir d;
  render::TemplateEngineConfig cfg;
  cfg.search_paths = {d.Path()};
  render::TemplateEngine eng(std::move(cfg));
  EventStream stream(eng);

  auto r = stream.Publish("never.bound", {{"x", 1}});
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, StreamError::UnknownTopic);
}

TEST(EventStream, PublishWithBoundTopicSucceedsEvenWithNoSubscribers) {
  TmpDir d;
  d.WriteFile("counter.html.inja",
              R"(<span id="c">{{ count }}</span>)");
  render::TemplateEngineConfig cfg;
  cfg.search_paths = {d.Path()};
  render::TemplateEngine eng(std::move(cfg));
  EventStream stream(eng);
  stream.Bind(MakeBinding("counter.tick", "counter", "c"));

  auto r = stream.Publish("counter.tick", {{"count", 7}});
  ASSERT_TRUE(r.has_value()) << r.error().message;
  EXPECT_EQ(stream.SubscriberCount(), 0u);
}

TEST(EventStream, SubscriberCountStartsAtZero) {
  TmpDir d;
  render::TemplateEngineConfig cfg;
  cfg.search_paths = {d.Path()};
  render::TemplateEngine eng(std::move(cfg));
  EventStream stream(eng);
  EXPECT_EQ(stream.SubscriberCount(), 0u);
}

TEST(EventStream, PublishSurfacesTemplateError) {
  TmpDir d;
  // No template file written; Render should fail.
  render::TemplateEngineConfig cfg;
  cfg.search_paths = {d.Path()};
  render::TemplateEngine eng(std::move(cfg));
  EventStream stream(eng);
  stream.Bind(MakeBinding("t", "missing", "x"));

  auto r = stream.Publish("t", {});
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, StreamError::TemplateFailed);
}

TEST(EventStream, BindOverwritesExistingTopic) {
  TmpDir d;
  d.WriteFile("a.html.inja", "from-A");
  d.WriteFile("b.html.inja", "from-B");
  render::TemplateEngineConfig cfg;
  cfg.search_paths = {d.Path()};
  render::TemplateEngine eng(std::move(cfg));
  EventStream stream(eng);

  stream.Bind(MakeBinding("dup", "a", "x"));
  stream.Bind(MakeBinding("dup", "b", "x"));
  // Re-binding a topic should not error; the second wins. We can't
  // observe the rendered body without a subscriber, but the call
  // should still succeed (no UnknownTopic).
  auto r = stream.Publish("dup", {});
  ASSERT_TRUE(r.has_value()) << r.error().message;
}

}  // namespace einheit::ui
