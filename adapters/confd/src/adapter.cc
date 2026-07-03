/// @file adapter.cc
/// @brief confd UI adapter — the reference "UI drives the CLI command
/// engine" implementation.
///
/// Routes split cleanly along the management-plane rule:
///   * mutations (configure / set / delete / commit / commit-confirmed
///     / confirm / rollback) go through `ui::CommandDriver` → the
///     shared command engine → the confd Runtime → the MemoryBackend.
///     The engine gates role and emits the audit record, once.
///   * reads (running config + the commit-confirmed countdown) come
///     straight off the Runtime — the fast direct path — so the live
///     UI stays responsive and the audit log stays a log of mutations.
// Copyright (c) 2026 Einheit Networks

#include "einheit/adapters/confd/ui_adapter.h"

#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "einheit/cli/audit.h"
#include "einheit/cli/auth.h"
#include "einheit/cli/confd/memory_backend.h"
#include "einheit/cli/confd/runtime.h"
#include "einheit/cli/transport/inproc.h"
#include "einheit/ui/command_driver.h"
#include "einheit/ui/route.h"
#include "einheit/ui/stream.h"

namespace einheit::adapters::confd {
namespace {

namespace cli = einheit::cli;

/// Newest-first cap for the on-screen audit trail.
constexpr std::size_t kAuditTail = 25;

/// Current UTC time in epoch milliseconds — matches confd's deadline
/// clock so the countdown maths lines up with the runtime's timer.
auto NowMs() -> std::int64_t {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

/// Extract a request field from a urlencoded form body, a JSON body,
/// or the query string (in that order). Small and dependency-free so
/// both browsers and tests can post either shape.
auto Field(const crow::request &req, const std::string &key)
    -> std::string {
  const auto ct = req.get_header_value("Content-Type");
  if (ct.find("application/json") != std::string::npos) {
    auto j = nlohmann::json::parse(req.body, nullptr, false);
    if (j.is_object()) return j.value(key, std::string{});
  }
  auto decode = [](const std::string &raw) {
    std::string out;
    for (std::size_t i = 0; i < raw.size(); ++i) {
      if (raw[i] == '+') {
        out += ' ';
      } else if (raw[i] == '%' && i + 2 < raw.size()) {
        out += static_cast<char>(
            std::stoi(raw.substr(i + 1, 2), nullptr, 16));
        i += 2;
      } else {
        out += raw[i];
      }
    }
    return out;
  };
  const auto pos = req.body.find(key + "=");
  if (pos != std::string::npos) {
    const auto start = pos + key.size() + 1;
    const auto end = req.body.find('&', start);
    return decode(end == std::string::npos
                      ? req.body.substr(start)
                      : req.body.substr(start, end - start));
  }
  if (const auto *q = req.url_params.get(key); q) return q;
  return {};
}

/// Map a driver outcome onto an HTTP status. A role rejection is a 403
/// exactly as the CLI would refuse it; a missing candidate session is
/// a 409; a daemon-level rejection is a 400; transport trouble is 502.
auto HttpStatus(ui::DriveStatus s) -> int {
  switch (s) {
    case ui::DriveStatus::Ok:
      return 200;
    case ui::DriveStatus::RoleForbidden:
      return 403;
    case ui::DriveStatus::SessionRequired:
      return 409;
    case ui::DriveStatus::DaemonError:
    case ui::DriveStatus::NotAWireCommand:
    case ui::DriveStatus::UnknownCommand:
      return 400;
    case ui::DriveStatus::TransportUnavailable:
    case ui::DriveStatus::WireTimeout:
    case ui::DriveStatus::WireFailed:
      return 502;
  }
  return 500;
}

class ConfdUiAdapter final : public ui::ProductUiAdapter {
 public:
  explicit ConfdUiAdapter(ConfdConfig cfg)
      : cfg_(std::move(cfg)),
        backend_(nullptr),
        runtime_(backend_, MakeRuntimeOptions()) {
    tx_ = cli::transport::NewInProcTransport(
        [this](const cli::protocol::Request &r) {
          return runtime_.HandleRequest(r);
        });
    (void)tx_->Connect();
    driver_ = std::make_unique<ui::CommandDriver>(
        ui::BuildConfdCommandTree(), tx_.get(), AuditSink());
  }

  auto Slug() const -> std::string override { return "confd"; }
  auto DisplayName() const -> std::string override {
    return "confd";
  }
  auto TemplatesDir() const -> std::string override {
    return EINHEIT_UI_ADAPTER_CONFD_TEMPLATES_DIR;
  }
  auto Nav() const -> std::vector<ui::NavEntry> override {
    return {{"/config", "Config", "config", "git-branch"}};
  }

  auto Mount(ui::AdapterContext ctx) -> void override {
    events_ = ctx.events;
    auto *eng = ctx.templates;
    auto &app = *ctx.app;
    auto nav = Nav();

    // ---- reads: fast direct path off the Runtime ----
    auto page = [this, eng, nav](const crow::request &req) {
      ui::RenderArgs args;
      args.fragment = "confd/config";
      args.layout = "layout";
      args.data = StatusJson("");
      args.meta = {{"title", "confd — config"},
                   {"brand", DisplayName()},
                   {"active", "config"},
                   {"nav", ui::NavToJson(nav)}};
      auto r = ui::Render(*eng, req, args);
      if (!r) {
        return ui::RenderError(*eng, req, 500, "render_failed",
                               r.error().message);
      }
      return std::move(*r);
    };
    CROW_ROUTE(app, "/")(page);
    CROW_ROUTE(app, "/config")(page);

    // The live status region: a reconnecting browser polls this to see
    // the current candidate state and the commit-confirmed countdown.
    CROW_ROUTE(app, "/config/status")
    ([this, eng](const crow::request &) {
      return StatusFragment(*eng, "");
    });

    // ---- mutations: every one goes through the command engine ----
    CROW_ROUTE(app, "/config/configure")
        .methods("POST"_method)([this, eng](const crow::request &req) {
          return DriveAndRender(*eng, req, "configure", {});
        });

    CROW_ROUTE(app, "/config/set")
        .methods("POST"_method)([this, eng](const crow::request &req) {
          return DriveAndRender(*eng, req, "set",
                                {Field(req, "path"), Field(req, "value")});
        });

    CROW_ROUTE(app, "/config/delete")
        .methods("POST"_method)([this, eng](const crow::request &req) {
          return DriveAndRender(*eng, req, "delete", {Field(req, "path")});
        });

    CROW_ROUTE(app, "/config/commit")
        .methods("POST"_method)([this, eng](const crow::request &req) {
          return DriveAndRender(*eng, req, "commit", {});
        });

    CROW_ROUTE(app, "/config/commit-confirmed")
        .methods("POST"_method)([this, eng](const crow::request &req) {
          return DriveAndRender(*eng, req, "commit confirmed",
                                {Field(req, "minutes")});
        });

    CROW_ROUTE(app, "/config/confirm")
        .methods("POST"_method)([this, eng](const crow::request &req) {
          return DriveAndRender(*eng, req, "confirm", {});
        });

    CROW_ROUTE(app, "/config/rollback")
        .methods("POST"_method)([this, eng](const crow::request &req) {
          const auto rev = Field(req, "rev");
          if (rev.empty()) {
            return DriveAndRender(*eng, req, "rollback previous", {});
          }
          return DriveAndRender(*eng, req, "rollback to", {rev});
        });
  }

 private:
  auto MakeRuntimeOptions() const -> cli::confd::RuntimeOptions {
    cli::confd::RuntimeOptions opts;
    opts.state_dir = cfg_.state_dir;
    return opts;
  }

  // The engine writes one Record here per mutation (success or
  // rejection). This is the single audit trail the UI surfaces — the
  // same records the shell would produce for the same commands.
  auto AuditSink() -> cli::audit::Sink {
    return [this](const cli::audit::Record &rec) {
      std::lock_guard<std::mutex> lk(audit_mu_);
      audit_tail_.push_back(rec);
      if (audit_tail_.size() > kAuditTail) {
        audit_tail_.erase(audit_tail_.begin());
      }
    };
  }

  // Build the full read-side context: candidate state, running config,
  // the live countdown, and the recent audit trail. All reads are
  // direct — none of this goes through the engine.
  auto StatusJson(const std::string &message) const -> nlohmann::json {
    const auto running = runtime_.Running();
    const auto pending = runtime_.PendingConfirmState();

    nlohmann::json rows = nlohmann::json::array();
    for (const auto &[k, v] : running) {
      rows.push_back({{"key", k}, {"value", v}});
    }

    nlohmann::json pend;
    if (pending.armed) {
      const auto remaining_ms = pending.deadline_epoch_ms - NowMs();
      const auto secs = remaining_ms > 0 ? remaining_ms / 1000 : 0;
      pend = {{"armed", true},
              {"seconds", secs},
              {"mmss", std::format("{}m{:02d}s", secs / 60, secs % 60)},
              {"commit", pending.pending_commit},
              {"rollback_to", pending.rollback_to}};
    } else {
      pend = {{"armed", false},   {"seconds", 0}, {"mmss", "0m00s"},
              {"commit", 0},      {"rollback_to", 0}};
    }

    nlohmann::json audit = nlohmann::json::array();
    {
      std::lock_guard<std::mutex> lk(audit_mu_);
      for (auto it = audit_tail_.rbegin(); it != audit_tail_.rend(); ++it) {
        audit.push_back({{"user", it->user},
                         {"role", it->role},
                         {"command", it->command},
                         {"outcome", it->outcome},
                         {"ok", it->ok}});
      }
    }

    return {{"in_configure", driver_->InConfigure()},
            {"session_id", driver_->SessionId().value_or("")},
            {"running", std::move(rows)},
            {"running_count", running.size()},
            {"pending", std::move(pend)},
            {"audit", std::move(audit)},
            {"message", message}};
  }

  // Resolve the acting operator from the request. Identity + role ride
  // in on headers (a real deployment sets these from the authenticated
  // session); they propagate into the engine so gating and the audit
  // record attribute to the right person — CLI/UI parity.
  auto Caller(const crow::request &req) const
      -> cli::auth::CallerIdentity {
    auto hdr = [&](const char *name, const char *fallback) {
      const auto v = req.get_header_value(name);
      return v.empty() ? std::string(fallback) : v;
    };
    return ui::MakeCaller(hdr("X-Einheit-User", "operator"),
                          hdr("X-Einheit-Role", "operator"),
                          req.remote_ip_address);
  }

  // Render just the live status region (HTMX target #confd-status).
  auto StatusFragment(const ui::render::TemplateEngine &eng,
                      const std::string &message) -> crow::response {
    ui::RenderArgs args;
    args.fragment = "confd/status";
    args.data = StatusJson(message);
    auto r = ui::Render(eng, ui::ResponseFormat::Fragment, args);
    if (!r) return crow::response(500, r.error().message);
    return std::move(*r);
  }

  // The mutation path: drive the command through the engine, then
  // render the refreshed status region with the outcome mapped onto an
  // HTTP status. Connected browsers also get a toast.
  auto DriveAndRender(const ui::render::TemplateEngine &eng,
                      const crow::request &req, const std::string &path,
                      std::vector<std::string> args) -> crow::response {
    const auto result = driver_->Drive(Caller(req), path, std::move(args));
    if (events_) {
      events_->PublishToast(result.ok ? "good" : "bad",
                            std::format("{}: {}", result.command,
                                        result.ok ? "ok" : result.outcome));
    }
    const auto msg =
        result.ok ? std::format("{} ok", result.command)
                  : std::format("{} rejected: {} ({})", result.command,
                                result.outcome, result.message);
    auto resp = StatusFragment(eng, msg);
    resp.code = HttpStatus(result.status);
    // Surface the engine's verdict for programmatic callers / tests.
    resp.set_header("X-Confd-Outcome", result.outcome);
    return resp;
  }

  ConfdConfig cfg_;
  cli::confd::MemoryBackend backend_;
  cli::confd::Runtime runtime_;
  std::unique_ptr<cli::transport::InProcTransport> tx_;
  std::unique_ptr<ui::CommandDriver> driver_;
  ui::EventStream *events_ = nullptr;
  mutable std::mutex audit_mu_;
  std::vector<cli::audit::Record> audit_tail_;
};

}  // namespace

auto NewConfdUiAdapter(ConfdConfig cfg)
    -> std::unique_ptr<ui::ProductUiAdapter> {
  return std::make_unique<ConfdUiAdapter>(std::move(cfg));
}

}  // namespace einheit::adapters::confd
