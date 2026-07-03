/// @file server.h
/// @brief Crow server bring-up. Wraps `crow::SimpleApp` with the
/// framework's defaults: TLS, request id middleware, structured
/// logging, and a static-asset mount for the JS/CSS bundle.
// Copyright (c) 2026 Einheit Networks

#ifndef INCLUDE_EINHEIT_UI_SERVER_H_
#define INCLUDE_EINHEIT_UI_SERVER_H_

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

#include <crow.h>

#include "einheit/ui/error.h"

namespace einheit::ui {

/// Errors raised during server bring-up.
enum class ServerError {
  /// Listen socket bind failed.
  BindFailed,
  /// TLS configuration could not be applied (cert/key missing).
  TlsConfigFailed,
  /// Static asset directory missing.
  AssetsMissing,
};

/// Server configuration. Sensible defaults except where noted.
struct ServerConfig {
  /// TCP listen address.
  std::string bind_addr = "0.0.0.0";
  /// TCP listen port.
  std::uint16_t port = 7542;
  /// PEM-encoded server certificate path. Empty disables TLS.
  std::string tls_cert_path;
  /// PEM-encoded server private key path. Empty disables TLS.
  std::string tls_key_path;
  /// Filesystem path to the static asset directory (htmx, uplot,
  /// base.css, fonts). Mounted at `/assets/`.
  std::string assets_dir;
  /// Number of Crow worker threads. 0 = std::thread::hardware_concurrency().
  std::uint16_t worker_threads = 0;
  /// Install the framework's signal regime (SIGPIPE-ignore, fault
  /// diagnostics, USR2/HUP flags) during Configure(). Leave true for
  /// the real server; tests that don't want process-global signal
  /// changes set it false.
  bool install_signals = true;
  /// When true, uncaught-exception responses include the exception
  /// text (developer convenience). When false (production default),
  /// the client sees only a generic message and the detail goes to
  /// the server log — no internal detail leaks to the wire.
  bool debug_errors = false;
};

/// Apply the framework's defaults onto an existing Crow app. Adapter
/// code calls this once after constructing the app, before adding
/// routes. Idempotent.
/// @param app Crow app.
/// @param cfg Server configuration.
/// @returns void on success or ServerError.
auto Configure(crow::SimpleApp &app, const ServerConfig &cfg)
    -> std::expected<void, Error<ServerError>>;

/// Block-and-run the configured Crow app on `cfg.port`. Returns when
/// the app stops (e.g. on signal).
/// @param app Crow app.
/// @param cfg Server configuration.
/// @returns void on clean exit, or ServerError if startup fails.
auto Run(crow::SimpleApp &app, const ServerConfig &cfg)
    -> std::expected<void, Error<ServerError>>;

}  // namespace einheit::ui

#endif  // INCLUDE_EINHEIT_UI_SERVER_H_
