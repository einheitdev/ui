/// @file template_engine.h
/// @brief Inja template wrapper. Adapters and core code render HTML
/// fragments by name through a shared engine that handles search
/// paths, hot reload (debug), and a small library of registered
/// callbacks (theme variables, semantic class names, time
/// formatting).
///
/// Templates live on disk under a templates root; both the framework
/// shipped partials and per-adapter templates resolve against the
/// same engine so a `{% include "partials/card" %}` works regardless
/// of which directory the calling template lives in.
// Copyright (c) 2026 Einheit Networks

#ifndef INCLUDE_EINHEIT_UI_RENDER_TEMPLATE_ENGINE_H_
#define INCLUDE_EINHEIT_UI_RENDER_TEMPLATE_ENGINE_H_

#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "einheit/ui/error.h"

namespace einheit::ui::render {

/// Errors raised by template loading or rendering.
enum class TemplateError {
  /// Template name resolved to no file under any search path.
  NotFound,
  /// inja failed to parse the template (syntax error).
  ParseFailed,
  /// inja raised during rendering (missing variable, type mismatch).
  RenderFailed,
  /// I/O failure reading from disk.
  IoFailed,
};

/// Settings for a TemplateEngine instance.
struct TemplateEngineConfig {
  /// Ordered template search roots. Earlier entries shadow later
  /// ones so an adapter can override a framework partial by placing
  /// a same-named file in its own templates dir.
  std::vector<std::string> search_paths;
  /// File suffix appended when a template is referenced by bare name
  /// (e.g. "partials/card" -> "partials/card.html.inja").
  std::string default_suffix = ".html.inja";
  /// Reload templates from disk on every render. Debug only.
  bool hot_reload = false;
  /// Auto-escape `{{ var }}` interpolation as HTML. On by default.
  bool auto_escape = true;
};

/// Opaque engine handle. Holds the inja Environment and any cached
/// parsed templates.
class TemplateEngine {
 public:
  /// @param cfg Engine configuration.
  explicit TemplateEngine(TemplateEngineConfig cfg);
  ~TemplateEngine();

  TemplateEngine(const TemplateEngine &) = delete;
  auto operator=(const TemplateEngine &) -> TemplateEngine & = delete;
  TemplateEngine(TemplateEngine &&) noexcept;
  auto operator=(TemplateEngine &&) noexcept -> TemplateEngine &;

  /// Render a template by name with the given JSON context.
  /// @param name Logical template name (no suffix).
  /// @param ctx JSON object passed as the template root.
  /// @returns Rendered string or TemplateError with diagnostic.
  auto Render(std::string_view name, const nlohmann::json &ctx) const
      -> std::expected<std::string, Error<TemplateError>>;

  /// Render an inline template string (not loaded from disk). Useful
  /// for SSE message bodies or dev-mode error pages.
  /// @param source Raw template body.
  /// @param ctx JSON context.
  /// @returns Rendered string or TemplateError.
  auto RenderString(std::string_view source,
                    const nlohmann::json &ctx) const
      -> std::expected<std::string, Error<TemplateError>>;

  /// Resolve a logical template name to an absolute file path. Walks
  /// the search paths in order. Returns NotFound if no file matches.
  /// @param name Logical name.
  /// @returns Absolute path or TemplateError::NotFound.
  auto Resolve(std::string_view name) const
      -> std::expected<std::string, Error<TemplateError>>;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace einheit::ui::render

#endif  // INCLUDE_EINHEIT_UI_RENDER_TEMPLATE_ENGINE_H_
