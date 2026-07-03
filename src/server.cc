/// @file server.cc
// Copyright (c) 2026 Einheit Networks

#include "einheit/ui/server.h"

#include <filesystem>
#include <format>
#include <thread>
#include <utility>

#include <exception>

#include <spdlog/spdlog.h>

#include "einheit/ui/signals.h"
#include "einheit/ui/static_files.h"

namespace einheit::ui {
namespace {

auto MakeError(ServerError code, std::string msg)
    -> Error<ServerError> {
  return Error<ServerError>{code, std::move(msg)};
}

// Server-wide last-resort exception net. Crow invokes this for any
// exception that escapes a route handler. The per-route helpers
// (Render + RenderError) already turn *expected* failures into clean,
// format-aware error pages; this catches the *unexpected* throw — a
// nlohmann type_error on a malformed daemon response, a std::bad_alloc,
// an adapter bug — so it becomes a logged 500 instead of a bland
// Crow default (or, for a future non-Crow caller, a crash). Runs on
// the worker thread with the faulting stack unwound; it has the
// response but not the request, so context beyond the exception text
// comes from the route-level logs.
void InstallExceptionHandler(crow::SimpleApp &app, bool debug_errors) {
  app.exception_handler([debug_errors](crow::response &res) {
    std::string detail;
    try {
      throw;
    } catch (const std::exception &e) {
      detail = e.what();
      spdlog::error("uncaught exception in request handler: {}", detail);
    } catch (...) {
      detail = "unknown exception type";
      spdlog::error("uncaught non-std exception in request handler");
    }
    // Prod-safe body by default; detail only when explicitly asked.
    // No hung connection, no blank 500 — always a bounded plaintext
    // response the client can read.
    res = crow::response(500);
    res.body = debug_errors
                   ? ("internal error: " + detail)
                   : std::string("internal error");
    res.set_header("Content-Type", "text/plain; charset=utf-8");
  });
}

}  // namespace

auto Configure(crow::SimpleApp &app, const ServerConfig &cfg)
    -> std::expected<void, Error<ServerError>> {
  if (cfg.install_signals) {
    // SIGPIPE-ignore is the single highest-value line in this file:
    // without it a client that drops mid-write kills the server.
    InstallSignalHandlers();
  }
  InstallExceptionHandler(app, cfg.debug_errors);

  const auto threads =
      cfg.worker_threads == 0
          ? std::max(1u, std::thread::hardware_concurrency())
          : cfg.worker_threads;
  app.concurrency(threads);

  if (!cfg.assets_dir.empty()) {
    if (!std::filesystem::exists(cfg.assets_dir)) {
      return std::unexpected(MakeError(
          ServerError::AssetsMissing,
          std::format("assets dir '{}' missing", cfg.assets_dir)));
    }
    MountStatic(app, "/assets", cfg.assets_dir);
  }

  return {};
}

auto Run(crow::SimpleApp &app, const ServerConfig &cfg)
    -> std::expected<void, Error<ServerError>> {
#ifdef CROW_ENABLE_SSL
  if (!cfg.tls_cert_path.empty() && !cfg.tls_key_path.empty()) {
    app.bindaddr(cfg.bind_addr).port(cfg.port).ssl_file(
        cfg.tls_cert_path, cfg.tls_key_path);
    spdlog::info("einheit-ui listening on https://{}:{}",
                 cfg.bind_addr, cfg.port);
    app.run();
    return {};
  }
#endif
  if (!cfg.tls_cert_path.empty() || !cfg.tls_key_path.empty()) {
    return std::unexpected(MakeError(
        ServerError::TlsConfigFailed,
        "both --tls-cert and --tls-key must be provided"));
  }
  app.bindaddr(cfg.bind_addr).port(cfg.port);
  spdlog::info("einheit-ui listening on http://{}:{}",
               cfg.bind_addr, cfg.port);
  app.run();
  return {};
}

}  // namespace einheit::ui
