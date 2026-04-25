/// @file adapter.cc
/// @brief Shell adapter wiring. /shell renders the xterm.js page;
/// /shell/ws is the per-session PTY-bridged WebSocket.
// Copyright (c) 2026 Einheit Networks

#include "einheit/adapters/shell/ui_adapter.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <crow/websocket.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "adapters/shell/src/pty_session.h"
#include "einheit/ui/error.h"
#include "einheit/ui/route.h"

namespace einheit::adapters::shell {
namespace {

auto MakeError(ShellError code, std::string message)
    -> ui::Error<ShellError> {
  return ui::Error<ShellError>{code, std::move(message)};
}

// One session per WebSocket connection. Crow keeps connection
// pointers stable for the lifetime of the connection, so a raw
// pointer key is fine; the unique_ptr value owns the PTY child.
struct SessionRegistry {
  std::mutex mu;
  std::unordered_map<crow::websocket::connection *,
                      std::unique_ptr<PtySession>>
      sessions;
};

}  // namespace

auto TemplatesDir() -> std::string {
  return EINHEIT_UI_ADAPTER_SHELL_TEMPLATES_DIR;
}

auto Mount(crow::SimpleApp &app,
           const ui::render::TemplateEngine &engine,
           const ShellConfig &cfg)
    -> std::expected<void, ui::Error<ShellError>> {
  // Validate launcher + cli paths up front so we fail at mount,
  // not on the first WebSocket connection. This keeps server
  // startup honest about its capabilities.
  namespace fs = std::filesystem;
  if (cfg.launcher_path.empty() || !fs::exists(cfg.launcher_path)) {
    return std::unexpected(MakeError(
        ShellError::LauncherNotFound,
        "einheit-shell-launcher not found at: " +
            cfg.launcher_path));
  }
  if (cfg.cli_path.empty() || !fs::exists(cfg.cli_path)) {
    return std::unexpected(MakeError(
        ShellError::CliNotFound,
        "einheit cli not found at: " + cfg.cli_path));
  }

  auto registry = std::make_shared<SessionRegistry>();

  // GET /shell — the xterm.js page. Task 5 vendors xterm.js into
  // assets/ and wires the fit + theme integration. Today the
  // template just renders a minimal page with the WS endpoint
  // baked in.
  CROW_ROUTE(app, "/shell")
  ([&engine, cfg](const crow::request &req) {
    ui::RenderArgs args;
    args.fragment = "shell/terminal";
    args.layout = "layout";
    args.data = {
        {"ws_path", "/shell/ws"},
        {"adapter", cfg.cli_adapter},
    };
    args.meta = {
        {"title", "shell"},
        {"brand", cfg.primary_brand},
        {"active", "shell"},
        {"nav", ui::NavToJson(cfg.primary_nav)},
    };
    auto r = ui::Render(engine, req, args);
    if (!r) {
      return ui::RenderError(engine, req, 500, "render_failed",
                             r.error().message);
    }
    return std::move(*r);
  });

  // Build a fresh PtyLaunchSpec from the deploy-time config.
  // Captured by onopen + onmessage so a respawn after
  // cli `exit` can use the same spec.
  auto make_spec = [cfg]() {
    PtyLaunchSpec spec;
    spec.launcher_path = cfg.launcher_path;
    spec.cli_path = cfg.cli_path;
    spec.adapter = cfg.cli_adapter;
    spec.role = cfg.default_role;
    spec.user = cfg.default_user;
    spec.uid = cfg.uid;
    spec.gid = cfg.gid;
    spec.target = cfg.cli_target;
    spec.endpoint = cfg.cli_endpoint;
    spec.event_endpoint = cfg.cli_event_endpoint;
    spec.learn = cfg.cli_learn;
    return spec;
  };

  auto make_sink = [](crow::websocket::connection &conn) {
    return [&conn](std::string_view chunk) {
      conn.send_text(std::string(chunk));
    };
  };

  // WS /shell/ws — one PTY per connection.
  CROW_WEBSOCKET_ROUTE(app, "/shell/ws")
      .onopen([registry, make_spec, make_sink](
                  crow::websocket::connection &conn) {
        auto session = std::make_unique<PtySession>();
        // Capturing &conn is safe — Crow guarantees the
        // connection outlives onopen/onmessage/onclose for this
        // socket, and the reader thread is joined in
        // PtySession's destructor before the unique_ptr is
        // erased from the registry inside onclose.
        if (auto r = session->Spawn(make_spec(), make_sink(conn));
            !r) {
          spdlog::warn("shell session spawn failed: {}",
                       r.error().message);
          conn.send_text(
              "shell adapter: failed to start cli — see "
              "server logs.\r\n");
          conn.close("spawn failed");
          return;
        }
        std::lock_guard<std::mutex> lk(registry->mu);
        registry->sessions[&conn] = std::move(session);
      })
      .onmessage(
          [registry, make_spec, make_sink](
              crow::websocket::connection &conn,
              const std::string &data, bool is_binary) {
            // Convention: text frames starting with `{` are JSON
            // control envelopes (resize); everything else (binary
            // OR plain-text input) is forwarded as keystrokes.
            const bool is_control =
                !is_binary && !data.empty() && data.front() == '{';

            // Look up + check liveness under the lock; release
            // before doing PTY I/O. If the cli exited (operator
            // typed `exit`), respawn a fresh session so the next
            // keystroke gets them back into a working shell —
            // resize controls in this state are silently dropped
            // since they're for the dead PTY.
            std::unique_ptr<PtySession> ended;
            PtySession *session = nullptr;
            {
              std::lock_guard<std::mutex> lk(registry->mu);
              auto it = registry->sessions.find(&conn);
              if (it == registry->sessions.end()) return;
              if (!it->second->IsRunning()) {
                ended = std::move(it->second);
              } else {
                session = it->second.get();
              }
            }

            if (ended) {
              ended.reset();  // Joins the dead reader thread.
              if (is_control) return;  // Don't respawn for resizes.
              auto fresh = std::make_unique<PtySession>();
              if (auto r = fresh->Spawn(make_spec(),
                                         make_sink(conn));
                  !r) {
                spdlog::warn("shell session respawn failed: {}",
                             r.error().message);
                conn.send_text("\r\nrespawn failed.\r\n");
                conn.close("respawn failed");
                return;
              }
              std::lock_guard<std::mutex> lk(registry->mu);
              registry->sessions[&conn] = std::move(fresh);
              return;
            }

            if (is_control) {
              try {
                auto j = nlohmann::json::parse(data);
                if (j.value("type", "") == "resize") {
                  session->Resize(
                      j.value<unsigned short>("rows", 24),
                      j.value<unsigned short>("cols", 80));
                  return;
                }
              } catch (const std::exception &) {
                // Fall through to treating it as input.
              }
            }
            session->Write(data);
          })
      .onclose([registry](crow::websocket::connection &conn,
                          const std::string & /*reason*/,
                          uint16_t /*code*/) {
        std::unique_ptr<PtySession> session;
        {
          std::lock_guard<std::mutex> lk(registry->mu);
          auto it = registry->sessions.find(&conn);
          if (it == registry->sessions.end()) return;
          session = std::move(it->second);
          registry->sessions.erase(it);
        }
        // Destructor reaps the child + joins the reader.
        session.reset();
      });

  return {};
}

}  // namespace einheit::adapters::shell
