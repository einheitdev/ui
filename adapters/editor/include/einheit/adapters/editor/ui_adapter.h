/// @file ui_adapter.h
/// @brief Source-editor adapter. Mounts a `/edit` page that hosts
/// a CodeMirror 6 instance plus a side analysis pane. The
/// scaffold is intentionally thin — language definitions and
/// per-section analysis (verifier output, eBPF chain split
/// previews, manual split markers) plug in via separate routes
/// per product, not here.
///
/// Free function rather than ProductUiAdapter for the same reason
/// as the shell adapter: the editor is a framework-level surface
/// that coexists with whichever product adapter is the primary
/// nav owner.
// Copyright (c) 2026 Einheit Networks

#ifndef INCLUDE_EINHEIT_ADAPTERS_EDITOR_UI_ADAPTER_H_
#define INCLUDE_EINHEIT_ADAPTERS_EDITOR_UI_ADAPTER_H_

#include <expected>
#include <string>

#include <crow.h>

#include "einheit/ui/error.h"
#include "einheit/ui/render/template_engine.h"

namespace einheit::adapters::editor {

/// Errors raised by the editor adapter at mount time.
enum class EditorError {
  /// Internal failure plumbing the route.
  RouteWiring,
};

/// Per-deploy editor configuration. Today only carries a starter
/// document so a fresh deploy renders something readable; future
/// fields will cover authoritative source paths, language ids,
/// and the analysis WS endpoint.
struct EditorConfig {
  /// Initial buffer contents shown when the page loads with no
  /// query parameters. Empty string is fine; the page renders an
  /// empty editor.
  std::string starter_text;
  /// Hint to client-side JS about which language extension to
  /// load (e.g. "fwl"). The framework today doesn't ship any
  /// grammars — products plug them in by extending the editor
  /// page client-side.
  std::string starter_lang;
};

/// Mount the editor page at `/edit`.
///
/// @param app Crow app the route attaches to.
/// @param engine Template engine for the page render.
/// @param cfg Per-deploy starter document + language hint.
/// @returns void on success, EditorError otherwise.
auto Mount(crow::SimpleApp &app,
           const ui::render::TemplateEngine &engine,
           const EditorConfig &cfg)
    -> std::expected<void, ui::Error<EditorError>>;

/// Path to the adapter's templates directory. Surfaced so the
/// binary can include it in the engine's search path.
auto TemplatesDir() -> std::string;

}  // namespace einheit::adapters::editor

#endif  // INCLUDE_EINHEIT_ADAPTERS_EDITOR_UI_ADAPTER_H_
