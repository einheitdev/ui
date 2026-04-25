/// @file route.h
/// @brief HTMX-aware route helpers around Crow. The framework's
/// stance is one logical handler per resource that returns the
/// shape the client asked for: full page, fragment-only HTML, or
/// JSON. The handler builds a JSON context once and `Render` picks
/// the right output.
// Copyright (c) 2026 Einheit Networks

#ifndef INCLUDE_EINHEIT_UI_ROUTE_H_
#define INCLUDE_EINHEIT_UI_ROUTE_H_

#include <expected>
#include <string>
#include <string_view>

#include <crow.h>
#include <nlohmann/json.hpp>

#include "einheit/ui/error.h"
#include "einheit/ui/render/template_engine.h"

namespace einheit::ui {

/// Errors raised by the route helpers.
enum class RouteError {
  /// Underlying template render failed.
  TemplateFailed,
  /// Caller asked for a format the framework does not implement.
  UnsupportedFormat,
};

/// What kind of body the client wants.
enum class ResponseFormat {
  /// Compact JSON. Selected by `Accept: application/json` or by
  /// `?format=json`.
  Json,
  /// HTMX partial — fragment template only, no layout.
  Fragment,
  /// Full page — layout template wrapping the fragment template.
  Page,
};

/// Inspect a Crow request and decide which response shape the
/// client is asking for. Precedence (highest first):
///   1. Explicit `?format=json|html` query parameter.
///   2. `HX-Request: true` header                  -> Fragment.
///   3. `Accept: application/json`                 -> Json.
///   4. Default                                    -> Page.
/// @param req Crow request.
/// @returns The chosen ResponseFormat.
auto DetectFormat(const crow::request &req) -> ResponseFormat;

/// Render context for a single route handler.
struct RenderArgs {
  /// Logical name of the fragment template (e.g. "f/rules").
  std::string fragment;
  /// Logical name of the layout template that wraps the fragment
  /// when ResponseFormat::Page is selected. Defaults to "layout".
  std::string layout = "layout";
  /// JSON data passed to both templates.
  nlohmann::json data = nlohmann::json::object();
  /// Page metadata — title, active nav slug — surfaced as `meta`
  /// in the layout template's context.
  nlohmann::json meta = nlohmann::json::object();
};

/// Build a Crow response from RenderArgs in the format the client
/// asked for. JSON returns `args.data` directly; Fragment renders
/// only `args.fragment`; Page renders `args.layout` with `body` set
/// to the rendered fragment and `meta` propagated.
/// @param eng Template engine.
/// @param fmt Selected response format.
/// @param args Render arguments.
/// @returns Crow response or RouteError.
auto Render(const render::TemplateEngine &eng, ResponseFormat fmt,
            const RenderArgs &args)
    -> std::expected<crow::response, Error<RouteError>>;

/// Convenience: detect format from `req` and call Render. Used by
/// most adapter handlers.
/// @param eng Template engine.
/// @param req Incoming request.
/// @param args Render arguments.
/// @returns Crow response or RouteError.
auto Render(const render::TemplateEngine &eng,
            const crow::request &req, const RenderArgs &args)
    -> std::expected<crow::response, Error<RouteError>>;

/// Build a Crow response carrying a structured error. Renders the
/// `error` fragment / page when the client wanted HTML; emits a
/// `{code, message, hint}` JSON body when the client wanted JSON.
/// @param eng Template engine.
/// @param req Incoming request.
/// @param status HTTP status code.
/// @param code Machine-readable error code.
/// @param message Human-readable message.
/// @param hint Optional actionable hint.
/// @returns Populated Crow response (never fails — falls back to
/// plaintext if the error template itself is missing).
auto RenderError(const render::TemplateEngine &eng,
                 const crow::request &req, int status,
                 std::string_view code, std::string_view message,
                 std::string_view hint = "") -> crow::response;

/// True iff the request carries `HX-Request: true`.
/// @param req Incoming request.
auto IsHxRequest(const crow::request &req) -> bool;

/// Set the path the layout's sidebar foot links to as "Shell".
/// Empty string hides the entry. The value is process-global; the
/// UI binary calls this once at startup when `--shell` is enabled
/// so /shell shows up in the sidebar of every product page
/// without each adapter having to know about the shell module.
/// @param path URL path; pass "" to hide.
auto SetLayoutShellPath(std::string path) -> void;

/// Read the current shell-path setting. Used by Render().
auto LayoutShellPath() -> std::string;

}  // namespace einheit::ui

#endif  // INCLUDE_EINHEIT_UI_ROUTE_H_
