/// @file adapter.cc
/// @brief Example adapter wiring. Three routes that show every
/// supported response shape:
///
///   GET  /            — full page (dashboard)
///   GET  /counters    — JSON or fragment depending on Accept/HX
///   POST /tick        — mutates state, publishes an SSE event so
///                       any browser viewing the dashboard sees the
///                       counter update without a refresh
// Copyright (c) 2026 Einheit Networks

#include "einheit/adapters/example/ui_adapter.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "einheit/ui/route.h"

namespace einheit::adapters::example {
namespace {

class ExampleUiAdapter final : public ui::ProductUiAdapter {
 public:
  auto Slug() const -> std::string override { return "example"; }
  auto DisplayName() const -> std::string override {
    return "example";
  }
  auto TemplatesDir() const -> std::string override {
    return EINHEIT_UI_ADAPTER_EXAMPLE_TEMPLATES_DIR;
  }
  auto Nav() const -> std::vector<ui::NavEntry> override {
    return {
        {"/", "Dashboard", "dashboard", "monitor"},
        {"/counters", "Counters", "counters", "activity"},
    };
  }

  auto Mount(ui::AdapterContext ctx) -> void override {
    ctx.events->Bind(ui::TopicBinding{
        .topic = "example.tick",
        .fragment = "example/counter_card",
        .swap_target = "counter-card",
        .swap_strategy = "outerHTML",
    });

    auto *eng = ctx.templates;
    auto *events = ctx.events;
    auto nav = Nav();
    auto &app = *ctx.app;

    CROW_ROUTE(app, "/")
    ([eng, this, nav](const crow::request &req) {
      ui::RenderArgs args;
      args.fragment = "example/dashboard";
      args.layout = "layout";
      args.data = {
          {"counter", counter_.load()},
          // partials/status rows.
          {"rows",
           {{{"label", "adapter"}, {"value", "example"},
             {"semantic", "info"}},
            {{"label", "build"}, {"value", "debug"}},
            {{"label", "uptime"}, {"value", "just now"},
             {"semantic", "good"}}}},
          // partials/button context.
          {"label", "tick"},
          // partials/log_entry context.
          {"entries",
           {{{"timestamp", "10:30:00"}, {"level", "INFO"},
             {"message", "engine started"}},
            {{"timestamp", "10:30:01"}, {"level", "DEBUG"},
             {"message", "websocket /events mounted"}}}},
      };
      args.data["attrs"] = nlohmann::json::object();
      args.meta = {
          {"title", "example — dashboard"},
          {"brand", DisplayName()},
          {"active", "dashboard"},
          {"nav", ui::NavToJson(nav)},
      };
      auto r = ui::Render(*eng, req, args);
      if (!r) {
        return ui::RenderError(*eng, req, 500, "render_failed",
                               r.error().message);
      }
      return std::move(*r);
    });

    CROW_ROUTE(app, "/counters")
    ([eng, this](const crow::request &req) {
      ui::RenderArgs args;
      args.fragment = "example/counter_card";
      args.layout = "layout";
      args.data = {{"counter", counter_.load()}};
      args.meta = {{"title", "counter"}, {"active", "counters"}};
      auto r = ui::Render(*eng, req, args);
      if (!r) {
        return ui::RenderError(*eng, req, 500, "render_failed",
                               r.error().message);
      }
      return std::move(*r);
    });

    CROW_ROUTE(app, "/tick").methods("POST"_method)(
        [eng, events, this](const crow::request &req) {
          const auto next = ++counter_;
          if (auto pub = events->Publish("example.tick",
                                          {{"counter", next}});
              !pub) {
            spdlog::warn("publish example.tick: {}",
                         pub.error().message);
          }
          ui::RenderArgs args;
          args.fragment = "example/counter_card";
          args.data = {{"counter", next}};
          auto r = ui::Render(*eng, ui::ResponseFormat::Fragment,
                              args);
          if (!r) {
            return ui::RenderError(*eng, req, 500, "render_failed",
                                   r.error().message);
          }
          return std::move(*r);
        });
  }

 private:
  std::atomic<std::uint64_t> counter_{0};
};

}  // namespace

auto NewExampleUiAdapter()
    -> std::unique_ptr<ui::ProductUiAdapter> {
  return std::make_unique<ExampleUiAdapter>();
}

}  // namespace einheit::adapters::example
