/// @file server.cc
// Copyright (c) 2026 Einheit Networks

#include "einheit/ui/server.h"

#include <filesystem>
#include <format>
#include <thread>
#include <utility>

#include <spdlog/spdlog.h>

#include "einheit/ui/static_files.h"

namespace einheit::ui {
namespace {

auto MakeError(ServerError code, std::string msg)
    -> Error<ServerError> {
  return Error<ServerError>{code, std::move(msg)};
}

}  // namespace

auto Configure(crow::SimpleApp &app, const ServerConfig &cfg)
    -> std::expected<void, Error<ServerError>> {
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
