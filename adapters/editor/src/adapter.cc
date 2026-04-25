/// @file adapter.cc
/// @brief Editor adapter wiring.
// Copyright (c) 2026 Einheit Networks

#include "einheit/adapters/editor/ui_adapter.h"

#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "einheit/ui/route.h"

namespace einheit::adapters::editor {

auto TemplatesDir() -> std::string {
  return EINHEIT_UI_ADAPTER_EDITOR_TEMPLATES_DIR;
}

auto Mount(crow::SimpleApp &app,
           const ui::render::TemplateEngine &engine,
           const EditorConfig &cfg)
    -> std::expected<void, ui::Error<EditorError>> {
  CROW_ROUTE(app, "/edit")
  ([&engine, cfg](const crow::request &req) {
    ui::RenderArgs args;
    args.fragment = "editor/page";
    args.layout = "layout";
    args.data = {
        {"starter_text", cfg.starter_text},
        {"starter_lang", cfg.starter_lang},
    };
    args.meta = {
        {"title", "edit"},
        {"active", "edit"},
    };
    auto r = ui::Render(engine, req, args);
    if (!r) {
      return ui::RenderError(engine, req, 500, "render_failed",
                             r.error().message);
    }
    return std::move(*r);
  });
  return {};
}

}  // namespace einheit::adapters::editor
