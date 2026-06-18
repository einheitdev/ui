/// @file adapter.cc
/// @brief takt UI adapter. Proxies the takt REST API,
/// renders pages via inja templates, publishes live updates
/// over WebSocket by polling the SSE stream.
// Copyright (c) 2026 Einheit Networks

#include "einheit/adapters/takt/ui_adapter.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "einheit/ui/route.h"

namespace einheit::adapters::takt {
namespace {

/// Build a badge context for a status string.
auto StatusSemantic(const std::string &s) -> std::string {
  if (s == "passed" || s == "completed" ||
      s == "running" || s == "clean") {
    return "good";
  }
  if (s == "failed" || s == "cancelled" ||
      s == "error") {
    return "bad";
  }
  if (s == "queued" || s == "pending") {
    return "warn";
  }
  return "info";
}

/// Build status rows for the dashboard status grid.
auto DashboardSummary(
    const nlohmann::json &workspaces,
    const nlohmann::json &runs,
    const nlohmann::json &agents,
    const nlohmann::json &targets)
    -> nlohmann::json {
  nlohmann::json rows = nlohmann::json::array();
  rows.push_back({
      {"label", "workspaces"},
      {"value", std::to_string(workspaces.size())},
  });
  rows.push_back({
      {"label", "targets"},
      {"value", std::to_string(targets.size())},
  });
  std::size_t running = 0;
  for (const auto &r : runs) {
    if (r.value("status", "") == "running") ++running;
  }
  rows.push_back({
      {"label", "active runs"},
      {"value", std::to_string(running)},
      {"semantic", running > 0 ? "good" : "info"},
  });
  rows.push_back({
      {"label", "agents"},
      {"value", std::to_string(agents.size())},
  });
  return rows;
}

/// Build workspace table rows.
auto WorkspaceRows(const nlohmann::json &workspaces)
    -> nlohmann::json {
  nlohmann::json rows = nlohmann::json::array();
  for (const auto &ws : workspaces) {
    nlohmann::json row;
    row["name"] = ws.value("name", "");
    row["branch"] = ws.value("branch", "");
    auto repos = ws.value("repos", nlohmann::json::array());
    row["repo_count"] = repos.size();
    std::string repo_list;
    for (const auto &r : repos) {
      if (!repo_list.empty()) repo_list += ", ";
      repo_list += r.get<std::string>();
    }
    row["repos"] = repo_list;
    rows.push_back(std::move(row));
  }
  return rows;
}

/// Build target table rows.
auto TargetRows(const nlohmann::json &targets)
    -> nlohmann::json {
  nlohmann::json rows = nlohmann::json::array();
  for (const auto &t : targets) {
    nlohmann::json row;
    row["name"] = t.value("name", "");
    row["type"] = t.value("type", "");
    row["host"] = t.value("host", "");
    row["template"] = t.value("template", false);
    auto lock = t.value("lock", nlohmann::json{});
    if (!lock.is_null()) {
      row["claimed_by"] = lock.value("workspace", "");
      row["state_semantic"] = "warn";
    } else {
      row["claimed_by"] = "";
      row["state_semantic"] = "good";
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

/// Build run table rows.
auto RunRows(const nlohmann::json &runs)
    -> nlohmann::json {
  nlohmann::json rows = nlohmann::json::array();
  for (const auto &r : runs) {
    nlohmann::json row;
    row["id"] = r.value("id", 0);
    row["workspace"] = r.value("workspace", "");
    row["status"] = r.value("status", "");
    row["status_semantic"] =
        StatusSemantic(r.value("status", ""));
    row["trigger"] = r.value("trigger", "");
    row["created_at"] = r.value("created_at", "");
    rows.push_back(std::move(row));
  }
  return rows;
}

class TaktUiAdapter final : public ui::ProductUiAdapter {
 public:
  explicit TaktUiAdapter(TaktClientConfig cfg)
      : client_(std::move(cfg)) {}

  ~TaktUiAdapter() override {
    poller_stop_.store(true);
    if (poller_.joinable()) poller_.join();
  }

  auto Slug() const -> std::string override {
    return "takt";
  }
  auto DisplayName() const -> std::string override {
    return "takt";
  }
  auto TemplatesDir() const -> std::string override {
    return EINHEIT_UI_ADAPTER_TAKT_TEMPLATES_DIR;
  }
  auto Nav() const -> std::vector<ui::NavEntry> override {
    return {
        {"/", "Dashboard", "dashboard", "monitor"},
        {"/workspaces", "Workspaces", "workspaces",
         "git-branch"},
        {"/pipeline", "Pipeline", "pipeline",
         "git-commit"},
        {"/agents", "Agents", "agents", "bot"},
        {"/targets", "Targets", "targets", "server"},
        {"/runs", "Runs", "runs", "play"},
        {"/settings", "Settings", "settings",
         "settings"},
    };
  }

  auto Mount(ui::AdapterContext ctx) -> void override {
    auto *eng = ctx.templates;
    auto &app = *ctx.app;
    auto *events = ctx.events;

    StartPoller(events);

    // Bind WebSocket topics for live updates.
    events->Bind(ui::TopicBinding{
        .topic = "takt.runs",
        .fragment = "takt/runs_table",
        .swap_target = "runs-table",
        .swap_strategy = "outerHTML",
    });
    events->Bind(ui::TopicBinding{
        .topic = "takt.dashboard",
        .fragment = "takt/dashboard_summary",
        .swap_target = "dashboard-summary",
        .swap_strategy = "outerHTML",
    });

    // -- Dashboard --
    CROW_ROUTE(app, "/")
    ([eng, this](const crow::request &req) {
      auto workspaces =
          client_.Get("/api/workspaces");
      auto runs = client_.Get("/api/runs");
      auto agents = client_.Get("/api/agents");
      auto targets = client_.Get("/api/targets");
      if (!workspaces || !runs || !agents || !targets) {
        auto msg = !workspaces
            ? workspaces.error().message
            : !runs ? runs.error().message
            : !agents ? agents.error().message
                      : targets.error().message;
        return ui::RenderError(
            *eng, req, 502, "takt_unreachable", msg,
            "is the takt API server running?");
      }
      ui::RenderArgs args;
      args.fragment = "takt/dashboard";
      args.layout = "layout";
      args.data = {
          {"summary",
           DashboardSummary(*workspaces, *runs,
                            *agents, *targets)},
          {"workspaces", WorkspaceRows(*workspaces)},
          {"runs", RunRows(*runs)},
          {"targets", TargetRows(*targets)},
          {"agents", *agents},
      };
      auto r = ui::Render(*eng, req, args);
      if (!r) {
        return ui::RenderError(*eng, req, 500,
                               "render_failed",
                               r.error().message);
      }
      return std::move(*r);
    });

    // -- Workspaces --
    CROW_ROUTE(app, "/workspaces")
    ([eng, this](const crow::request &req) {
      auto workspaces =
          client_.Get("/api/workspaces");
      if (!workspaces) {
        return ui::RenderError(
            *eng, req, 502, "takt_unreachable",
            workspaces.error().message);
      }
      ui::RenderArgs args;
      args.fragment = "takt/workspaces";
      args.layout = "layout";
      args.data = {
          {"workspaces", WorkspaceRows(*workspaces)},
      };
      auto r = ui::Render(*eng, req, args);
      if (!r) {
        return ui::RenderError(*eng, req, 500,
                               "render_failed",
                               r.error().message);
      }
      return std::move(*r);
    });

    // -- Workspace detail --
    CROW_ROUTE(app, "/workspaces/<string>")
    ([eng, this](const crow::request &req,
                 std::string name) {
      auto status = client_.Get(std::format(
          "/api/workspaces/{}/status", name));
      if (!status) {
        return ui::RenderError(
            *eng, req, 502, "takt_unreachable",
            status.error().message);
      }
      nlohmann::json repos = nlohmann::json::array();
      for (const auto &s : *status) {
        nlohmann::json row;
        row["repo"] = s.value("repo", "");
        row["branch"] = s.value("branch", "");
        auto st = s.value("status", "");
        row["status"] = st;
        row["status_semantic"] = StatusSemantic(st);
        repos.push_back(std::move(row));
      }
      ui::RenderArgs args;
      args.fragment = "takt/workspace_detail";
      args.layout = "layout";
      args.data = {
          {"name", name}, {"repos", repos},
      };
      auto r = ui::Render(*eng, req, args);
      if (!r) {
        return ui::RenderError(*eng, req, 500,
                               "render_failed",
                               r.error().message);
      }
      return std::move(*r);
    });

    // -- Pipeline --
    CROW_ROUTE(app, "/pipeline")
    ([eng, this](const crow::request &req) {
      auto workspaces =
          client_.Get("/api/workspaces");
      if (!workspaces) {
        return ui::RenderError(
            *eng, req, 502, "takt_unreachable",
            workspaces.error().message);
      }
      nlohmann::json pipelines =
          nlohmann::json::array();
      for (const auto &ws : *workspaces) {
        auto name =
            ws.value("name", std::string{});
        auto steps = client_.Get(std::format(
            "/api/pipeline/{}", name));
        nlohmann::json entry;
        entry["workspace"] = name;
        entry["steps"] = steps ? *steps
                               : nlohmann::json::array();
        pipelines.push_back(std::move(entry));
      }
      ui::RenderArgs args;
      args.fragment = "takt/pipeline";
      args.layout = "layout";
      args.data = {{"pipelines", pipelines}};
      auto r = ui::Render(*eng, req, args);
      if (!r) {
        return ui::RenderError(*eng, req, 500,
                               "render_failed",
                               r.error().message);
      }
      return std::move(*r);
    });

    // -- Targets --
    CROW_ROUTE(app, "/targets")
    ([eng, this](const crow::request &req) {
      auto targets = client_.Get("/api/targets");
      if (!targets) {
        return ui::RenderError(
            *eng, req, 502, "takt_unreachable",
            targets.error().message);
      }
      ui::RenderArgs args;
      args.fragment = "takt/targets";
      args.layout = "layout";
      args.data = {
          {"targets", TargetRows(*targets)},
      };
      auto r = ui::Render(*eng, req, args);
      if (!r) {
        return ui::RenderError(*eng, req, 500,
                               "render_failed",
                               r.error().message);
      }
      return std::move(*r);
    });

    // -- Runs --
    CROW_ROUTE(app, "/runs")
    ([eng, this](const crow::request &req) {
      auto runs = client_.Get("/api/runs");
      if (!runs) {
        return ui::RenderError(
            *eng, req, 502, "takt_unreachable",
            runs.error().message);
      }
      ui::RenderArgs args;
      args.fragment = "takt/runs";
      args.layout = "layout";
      args.data = {{"runs", RunRows(*runs)}};
      auto r = ui::Render(*eng, req, args);
      if (!r) {
        return ui::RenderError(*eng, req, 500,
                               "render_failed",
                               r.error().message);
      }
      return std::move(*r);
    });

    // -- Run detail --
    CROW_ROUTE(app, "/runs/<int>")
    ([eng, this](const crow::request &req, int id) {
      auto run = client_.Get(
          std::format("/api/runs/{}", id));
      if (!run) {
        return ui::RenderError(
            *eng, req, 502, "takt_unreachable",
            run.error().message);
      }
      auto data = *run;
      if (data.contains("run")) {
        auto &r2 = data["run"];
        r2["status_semantic"] = StatusSemantic(
            r2.value("status", ""));
      }
      if (data.contains("steps")) {
        for (auto &s : data["steps"]) {
          s["status_semantic"] = StatusSemantic(
              s.value("status", ""));
        }
      }
      ui::RenderArgs args;
      args.fragment = "takt/run_detail";
      args.layout = "layout";
      args.data = data;
      auto r = ui::Render(*eng, req, args);
      if (!r) {
        return ui::RenderError(*eng, req, 500,
                               "render_failed",
                               r.error().message);
      }
      return std::move(*r);
    });

    // -- Step output --
    CROW_ROUTE(app, "/runs/<int>/steps/<int>")
    ([eng, this](const crow::request &req,
                 int run_id, int step_id) {
      auto output = client_.Get(std::format(
          "/api/runs/{}/steps/{}/output", run_id,
          step_id));
      auto step = client_.Get(std::format(
          "/api/runs/{}/steps/{}", run_id, step_id));
      if (!output || !step) {
        auto msg = !output
            ? output.error().message
            : step.error().message;
        return ui::RenderError(
            *eng, req, 502, "takt_unreachable", msg);
      }
      nlohmann::json entries =
          nlohmann::json::array();
      for (const auto &line : *output) {
        std::string level = "INFO";
        auto kind = line.value("kind", "text");
        if (kind == "error") level = "ERROR";
        else if (kind == "tool_use") level = "DEBUG";
        else if (kind == "thinking") level = "DEBUG";
        entries.push_back({
            {"timestamp", line.value("ts", "")},
            {"level", level},
            {"message", line.value("content", "")},
        });
      }
      ui::RenderArgs args;
      args.fragment = "takt/step_output";
      args.layout = "layout";
      args.data = {
          {"step", *step},
          {"entries", entries},
          {"run_id", run_id},
      };
      auto r = ui::Render(*eng, req, args);
      if (!r) {
        return ui::RenderError(*eng, req, 500,
                               "render_failed",
                               r.error().message);
      }
      return std::move(*r);
    });

    // -- Agents --
    CROW_ROUTE(app, "/agents")
    ([eng, this](const crow::request &req) {
      auto agents = client_.Get("/api/agents");
      if (!agents) {
        return ui::RenderError(
            *eng, req, 502, "takt_unreachable",
            agents.error().message);
      }
      ui::RenderArgs args;
      args.fragment = "takt/agents";
      args.layout = "layout";
      args.data = {{"agents", *agents}};
      auto r = ui::Render(*eng, req, args);
      if (!r) {
        return ui::RenderError(*eng, req, 500,
                               "render_failed",
                               r.error().message);
      }
      return std::move(*r);
    });

    // -- POST actions --
    CROW_ROUTE(app, "/targets/<string>/claim")
        .methods("POST"_method)(
            [this](const crow::request &req,
                   std::string name) {
              auto body = nlohmann::json::parse(
                  req.body, nullptr, false);
              auto workspace = body.value(
                  "workspace", std::string{});
              auto resp = client_.Post(
                  std::format(
                      "/api/targets/{}/claim", name),
                  nlohmann::json{
                      {"workspace", workspace}}
                      .dump());
              if (!resp) {
                return crow::response(502,
                    resp.error().message);
              }
              return crow::response(200,
                  resp->dump());
            });

    CROW_ROUTE(app, "/targets/<string>/release")
        .methods("POST"_method)(
            [this](const crow::request &,
                   std::string name) {
              auto resp = client_.Post(
                  std::format(
                      "/api/targets/{}/release",
                      name));
              if (!resp) {
                return crow::response(502,
                    resp.error().message);
              }
              return crow::response(200,
                  resp->dump());
            });

    CROW_ROUTE(app, "/runs/trigger")
        .methods("POST"_method)(
            [this](const crow::request &req) {
              auto body = nlohmann::json::parse(
                  req.body, nullptr, false);
              auto resp = client_.Post(
                  "/api/runs", body.dump());
              if (!resp) {
                return crow::response(502,
                    resp.error().message);
              }
              return crow::response(201,
                  resp->dump());
            });
  }

 private:
  /// Poll the takt API every 5s and push updates over
  /// WebSocket.
  void StartPoller(ui::EventStream *events) {
    if (!events) return;
    poller_ = std::thread([this, events]() {
      using namespace std::chrono;
      while (!poller_stop_.load(
          std::memory_order_relaxed)) {
        std::this_thread::sleep_for(seconds(5));
        if (poller_stop_.load(
                std::memory_order_relaxed)) {
          break;
        }
        auto runs = client_.Get("/api/runs?limit=10");
        if (runs) {
          events->Publish(
              "takt.runs", {{"runs", RunRows(*runs)}});
        }
        auto ws = client_.Get("/api/workspaces");
        auto agents = client_.Get("/api/agents");
        auto targets = client_.Get("/api/targets");
        if (ws && runs && agents && targets) {
          events->Publish("takt.dashboard",
              {{"summary", DashboardSummary(
                  *ws, *runs, *agents, *targets)}});
        }
      }
    });
  }

  TaktClient client_;
  std::thread poller_;
  std::atomic<bool> poller_stop_{false};
};

}  // namespace

auto NewTaktUiAdapter(TaktClientConfig cfg)
    -> std::unique_ptr<ui::ProductUiAdapter> {
  return std::make_unique<TaktUiAdapter>(
      std::move(cfg));
}

}  // namespace einheit::adapters::takt
