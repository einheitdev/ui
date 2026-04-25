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
#include "einheit/ui/adapter.h"
#include "einheit/ui/render/template_engine.h"
#include "einheit/ui/route.h"
#include "einheit/ui/server.h"
#include "einheit/ui/stream.h"
#include "einheit/ui/theme.h"

namespace {

auto ResolveTemplatesDir(const std::string &override)
    -> std::string {
  if (!override.empty()) return override;
#ifdef EINHEIT_UI_TEMPLATES_DIR
  if (std::filesystem::exists(EINHEIT_UI_TEMPLATES_DIR)) {
    return EINHEIT_UI_TEMPLATES_DIR;
  }
#endif
  // Fallback for in-tree dev: ../../templates relative to the
  // binary's source dir.
  return "templates";
}

auto ResolveAssetsDir(const std::string &override) -> std::string {
  if (!override.empty()) return override;
  return "assets";
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

  app.add_option("--bind", bind_addr, "Bind address");
  app.add_option("--port", port, "TCP port");
  app.add_option("--tls-cert", tls_cert, "TLS certificate path");
  app.add_option("--tls-key", tls_key, "TLS private key path");
  app.add_option("--adapter", adapter_name,
                 "Product adapter (example)");
  app.add_option("--templates", templates_dir,
                 "Override templates root");
  app.add_option("--assets", assets_dir,
                 "Override assets directory");
  app.add_option("--theme", theme_name,
                 "Theme name (psychotropic, light, ...)");

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError &e) {
    return app.exit(e);
  }

  std::unique_ptr<einheit::ui::ProductUiAdapter> adapter;
  if (adapter_name == "example") {
    adapter = einheit::adapters::example::NewExampleUiAdapter();
  } else {
    std::cerr << std::format("unknown adapter '{}'\n", adapter_name);
    return 1;
  }

  einheit::ui::render::TemplateEngineConfig tcfg;
  // Adapter dir first so it can shadow framework partials.
  tcfg.search_paths.push_back(adapter->TemplatesDir());
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
  events.Mount(crow_app, "/events");

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

  einheit::ui::AdapterContext ctx{
      .app = &crow_app, .templates = &engine, .events = &events};
  adapter->Mount(ctx);

  if (auto r = einheit::ui::Run(crow_app, scfg); !r) {
    std::cerr << std::format("server: {}\n", r.error().message);
    return 1;
  }
  return 0;
}
