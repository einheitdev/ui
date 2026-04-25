/// @file ui_adapter.h
/// @brief Web terminal adapter. Mounts a `/shell` page and a
/// `/shell/ws` WebSocket endpoint that bridges browser keystrokes
/// to a sandboxed `einheit-cli --locked` running under a PTY.
///
/// Designed as a free-function `Mount(...)` rather than a
/// ProductUiAdapter so it can coexist with whichever product
/// adapter (example, hd-relay, ...) is the primary nav owner —
/// operators reach the terminal at `/shell` regardless of which
/// product the binary is wrapping.
// Copyright (c) 2026 Einheit Networks

#ifndef INCLUDE_EINHEIT_ADAPTERS_SHELL_UI_ADAPTER_H_
#define INCLUDE_EINHEIT_ADAPTERS_SHELL_UI_ADAPTER_H_

#include <expected>
#include <string>

#include <crow.h>

#include "einheit/ui/error.h"
#include "einheit/ui/render/template_engine.h"

namespace einheit::adapters::shell {

/// Errors raised by the shell adapter at mount time.
enum class ShellError {
  /// Launcher binary not found / not executable.
  LauncherNotFound,
  /// Cli binary not found / not executable.
  CliNotFound,
  /// Internal failure plumbing the WebSocket route.
  RouteWiring,
};

/// Configuration the operator binary supplies. Every PTY session
/// the adapter spawns inherits these as launcher argv values.
struct ShellConfig {
  /// Absolute path to einheit-shell-launcher.
  std::string launcher_path;
  /// Absolute path to the einheit cli binary.
  std::string cli_path;
  /// Cli adapter to load: "example" | "hd-relay" | ...
  /// Defaults to "example" so smoke tests work without product
  /// state behind them.
  std::string cli_adapter = "example";
  /// Default role forwarded to the cli (`admin` | `operator` |
  /// `any`). Operator authentication on the UI side is out of
  /// scope here — once the framework grows real auth, this will
  /// come from the request principal instead.
  std::string default_role = "operator";
  /// Default operator user name. Same caveat as default_role.
  std::string default_user = "operator";
  /// Numeric uid the launcher drops to. 0 means "no drop"
  /// (development only). Production deploys MUST set this to a
  /// dedicated einheit-shell account.
  unsigned int uid = 0;
  /// Numeric gid for the same drop. Same caveat.
  unsigned int gid = 0;
  /// Optional --target forwarded to the cli.
  std::string cli_target;
  /// Optional --endpoint forwarded to the cli.
  std::string cli_endpoint;
  /// Optional --event-endpoint forwarded to the cli.
  std::string cli_event_endpoint;
};

/// Mount the shell page + WebSocket endpoint on `app`. Routes:
///   GET /shell        — renders the xterm.js terminal page
///   WS  /shell/ws     — per-connection PTY-bridged cli session
///
/// `engine` is used to render the page template. Templates live
/// under `adapters/shell/templates/shell/` and are picked up via
/// the engine's adapter-search-path mechanism.
///
/// @param app Crow app the routes attach to.
/// @param engine Template engine for the page render.
/// @param cfg Per-deploy launcher / cli configuration.
/// @returns void on success or a ShellError describing what
/// could not be wired.
auto Mount(crow::SimpleApp &app,
           const ui::render::TemplateEngine &engine,
           const ShellConfig &cfg)
    -> std::expected<void, ui::Error<ShellError>>;

/// Path to the adapter's templates directory. Surfaced so the
/// binary can include it in the engine's search path.
auto TemplatesDir() -> std::string;

}  // namespace einheit::adapters::shell

#endif  // INCLUDE_EINHEIT_ADAPTERS_SHELL_UI_ADAPTER_H_
