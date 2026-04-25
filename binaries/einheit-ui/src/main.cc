/// @file main.cc
/// @brief Top-level einheit-ui entry point. Parses argv, builds a
/// template engine pointed at the framework + adapter dirs, mounts
/// the selected adapter, and runs Crow.
// Copyright (c) 2026 Einheit Networks

#include <cstdlib>
#include <filesystem>
#include <format>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <CLI/CLI.hpp>
#include <crow.h>
#include <spdlog/spdlog.h>

#include "einheit/adapters/example/ui_adapter.h"
#include "einheit/adapters/hd_relay/ui_adapter.h"
#include "einheit/adapters/shell/ui_adapter.h"
#include "einheit/ui/adapter.h"
#include "einheit/ui/render/template_engine.h"
#include "einheit/ui/route.h"
#include "einheit/ui/server.h"
#include "einheit/ui/stream.h"
#include "einheit/ui/theme.h"

namespace {

/// Pick the first existing path from a candidate list. Falls back
/// to the last candidate (typically the cwd-relative default) even
/// if it doesn't exist so the caller still gets a useful error
/// from the eventual filesystem op.
auto FirstExisting(std::initializer_list<const char *> paths)
    -> std::string {
  const char *last = "";
  for (const char *p : paths) {
    if (!p || !*p) continue;
    last = p;
    if (std::filesystem::exists(p)) return p;
  }
  return last;
}

auto ResolveTemplatesDir(const std::string &override)
    -> std::string {
  if (!override.empty()) return override;
  return FirstExisting({
#ifdef EINHEIT_UI_TEMPLATES_DIR
      EINHEIT_UI_TEMPLATES_DIR,
#endif
#ifdef EINHEIT_UI_DEV_TEMPLATES_DIR
      EINHEIT_UI_DEV_TEMPLATES_DIR,
#endif
      "templates",
  });
}

auto ResolveAssetsDir(const std::string &override) -> std::string {
  if (!override.empty()) return override;
  return FirstExisting({
#ifdef EINHEIT_UI_INSTALLED_ASSETS_DIR
      EINHEIT_UI_INSTALLED_ASSETS_DIR,
#endif
#ifdef EINHEIT_UI_DEV_ASSETS_DIR
      EINHEIT_UI_DEV_ASSETS_DIR,
#endif
      "assets",
  });
}

}  // namespace

auto main(int argc, char **argv) -> int {
  CLI::App app{"einheit-ui — Einheit Networks web UI server"};
  std::string bind_addr = "127.0.0.1";
  std::uint16_t port = 7542;
  std::string tls_cert;
  std::string tls_key;
  std::string adapter_name = "example";
  std::string templates_dir;
  std::string assets_dir;
  std::string theme_name = "psychotropic";
  std::string hd_url = "http://127.0.0.1:9090";
  std::string hd_token;

  // Shell adapter (web terminal). Off unless --shell is passed.
  bool enable_shell = false;
  std::string shell_launcher_path;
  std::string shell_cli_path;
  std::string shell_adapter = "example";
  std::string shell_role = "operator";
  std::string shell_user = "operator";
  std::uint32_t shell_uid = 0;
  std::uint32_t shell_gid = 0;
  std::string shell_target;
  std::string shell_endpoint;
  std::string shell_event_endpoint;
  bool shell_learn = false;

  app.add_option("--bind", bind_addr, "Bind address");
  app.add_option("--port", port, "TCP port");
  app.add_option("--tls-cert", tls_cert, "TLS certificate path");
  app.add_option("--tls-key", tls_key, "TLS private key path");
  app.add_option("--adapter", adapter_name,
                 "Product adapter (example | hd-relay)");
  app.add_option("--templates", templates_dir,
                 "Override templates root");
  app.add_option("--assets", assets_dir,
                 "Override assets directory");
  app.add_option("--theme", theme_name,
                 "Theme name (psychotropic, light, ocean, "
                 "forest, solarized-dark, high-contrast)");
  app.add_option("--hd-url", hd_url,
                 "Hyper-DERP daemon HTTP base URL "
                 "(hd-relay adapter only)");
  app.add_option("--hd-token", hd_token,
                 "Bearer token for the hd metrics endpoint "
                 "(optional)");

  app.add_flag("--shell", enable_shell,
               "Mount the /shell web terminal alongside the "
               "primary adapter. Requires --shell-launcher and "
               "--shell-cli.");
  app.add_option("--shell-launcher", shell_launcher_path,
                 "Absolute path to einheit-shell-launcher");
  app.add_option("--shell-cli", shell_cli_path,
                 "Absolute path to the einheit cli binary");
  app.add_option("--shell-adapter", shell_adapter,
                 "Cli adapter for shell sessions (default 'example')");
  app.add_option("--shell-role", shell_role,
                 "Default role forwarded to cli (admin|operator|any)");
  app.add_option("--shell-user", shell_user,
                 "Operator name forwarded to cli (audit identity)");
  app.add_option("--shell-uid", shell_uid,
                 "Numeric uid the launcher drops to (0 = no drop)");
  app.add_option("--shell-gid", shell_gid,
                 "Numeric gid the launcher drops to");
  app.add_option("--shell-target", shell_target,
                 "Optional --target forwarded to cli");
  app.add_option("--shell-endpoint", shell_endpoint,
                 "Optional --endpoint forwarded to cli");
  app.add_option("--shell-event-endpoint", shell_event_endpoint,
                 "Optional --event-endpoint forwarded to cli");
  app.add_flag("--shell-learn", shell_learn,
               "Spawn cli sessions in --learn mode (in-process "
               "learning daemon — useful when no product daemon "
               "is listening, e.g. demos)");

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError &e) {
    return app.exit(e);
  }

  std::unique_ptr<einheit::ui::ProductUiAdapter> adapter;
  if (adapter_name == "example") {
    adapter = einheit::adapters::example::NewExampleUiAdapter();
  } else if (adapter_name == "hd-relay") {
    einheit::adapters::hd_relay::HdClientConfig hcfg;
    hcfg.base_url = hd_url;
    hcfg.bearer_token = hd_token;
    adapter = einheit::adapters::hd_relay::NewHdRelayUiAdapter(
        std::move(hcfg));
  } else {
    std::cerr << std::format("unknown adapter '{}'\n", adapter_name);
    return 1;
  }

  einheit::ui::render::TemplateEngineConfig tcfg;
  // Adapter dir first so it can shadow framework partials.
  tcfg.search_paths.push_back(adapter->TemplatesDir());
  // Shell adapter's templates dir (when enabled) sits next so its
  // shell/terminal partial resolves; falls back to framework if
  // not enabled.
  if (enable_shell) {
    tcfg.search_paths.push_back(
        einheit::adapters::shell::TemplatesDir());
  }
  tcfg.search_paths.push_back(ResolveTemplatesDir(templates_dir));
#ifdef EINHEIT_UI_TEMPLATE_HOT_RELOAD
  tcfg.hot_reload = true;
#endif
  einheit::ui::render::TemplateEngine engine(std::move(tcfg));

  crow::SimpleApp crow_app;
  einheit::ui::ServerConfig scfg;
  scfg.bind_addr = bind_addr;
  scfg.port = port;
  scfg.tls_cert_path = tls_cert;
  scfg.tls_key_path = tls_key;
  scfg.assets_dir = ResolveAssetsDir(assets_dir);
  if (auto r = einheit::ui::Configure(crow_app, scfg); !r) {
    std::cerr << std::format("server config: {}\n",
                             r.error().message);
    return 1;
  }

  einheit::ui::EventStream events(engine);
  events.Mount(crow_app);

  // Theme route — renders theme.css.inja with the selected palette.
  const auto theme = einheit::ui::NamedTheme(theme_name);
  CROW_ROUTE(crow_app, "/theme.css")
  ([&engine, theme](const crow::request &) {
    auto body = engine.Render("theme.css", einheit::ui::ToJson(theme));
    if (!body) {
      crow::response r{500, body.error().message};
      r.set_header("Content-Type", "text/plain; charset=utf-8");
      return r;
    }
    crow::response r{200, *body};
    r.set_header("Content-Type", "text/css; charset=utf-8");
    r.set_header("Cache-Control",
                 "public, max-age=300, must-revalidate");
    return r;
  });

  // Stamp the framework's layout fallbacks once. Routes that
  // forget meta.nav / meta.brand fall back to these so the
  // sidebar always has the right entries — adapters only need
  // to set meta when they want to override.
  einheit::ui::SetLayoutPrimaryNav(
      einheit::ui::NavToJson(adapter->Nav()));
  einheit::ui::SetLayoutPrimaryBrand(adapter->DisplayName());

  einheit::ui::AdapterContext ctx{
      .app = &crow_app, .templates = &engine, .events = &events};
  adapter->Mount(ctx);

  if (enable_shell) {
    einheit::adapters::shell::ShellConfig scfg2;
    scfg2.launcher_path = shell_launcher_path;
    scfg2.cli_path = shell_cli_path;
    scfg2.cli_adapter = shell_adapter;
    scfg2.default_role = shell_role;
    scfg2.default_user = shell_user;
    scfg2.uid = shell_uid;
    scfg2.gid = shell_gid;
    scfg2.cli_target = shell_target;
    scfg2.cli_endpoint = shell_endpoint;
    scfg2.cli_event_endpoint = shell_event_endpoint;
    scfg2.cli_learn = shell_learn;
    scfg2.primary_nav = adapter->Nav();
    scfg2.primary_brand = adapter->DisplayName();
    if (auto r = einheit::adapters::shell::Mount(crow_app, engine,
                                                   scfg2);
        !r) {
      std::cerr << std::format("shell adapter: {}\n",
                               r.error().message);
      return 1;
    }
    // Tell the framework's layout where the shell lives so every
    // product page renders a "Shell" link in the sidebar foot.
    einheit::ui::SetLayoutShellPath("/shell");
  }

  if (auto r = einheit::ui::Run(crow_app, scfg); !r) {
    std::cerr << std::format("server: {}\n", r.error().message);
    return 1;
  }
  return 0;
}
