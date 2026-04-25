/// @file adapter.h
/// @brief Product adapter contract. Each Einheit product (f, hd,
/// future) implements ProductUiAdapter and returns it from a factory
/// function. The top-level binary mounts the framework, then asks
/// the adapter to register routes, bind SSE topics, and contribute
/// nav entries.
///
/// Adapters own their domain types; they do not see Crow's
/// `request`/`response` types beyond what `Render(...)` returns.
/// They produce JSON contexts and template names, the framework
/// does the rest.
// Copyright (c) 2026 Einheit Networks

#ifndef INCLUDE_EINHEIT_UI_ADAPTER_H_
#define INCLUDE_EINHEIT_UI_ADAPTER_H_

#include <memory>
#include <string>
#include <vector>

#include <crow.h>
#include <nlohmann/json.hpp>

#include "einheit/ui/render/template_engine.h"
#include "einheit/ui/stream.h"

namespace einheit::ui {

/// One nav entry contributed by an adapter. Rendered into the
/// shared layout template as a left-rail sidebar item.
struct NavEntry {
  /// URL path (e.g. "/rules").
  std::string href;
  /// Display label.
  std::string label;
  /// Stable slug used by the layout to highlight the active entry.
  std::string slug;
  /// Lucide icon name from the vendored sprite at /assets/icons.svg
  /// (e.g. "monitor", "users", "git-branch"). Empty string falls
  /// back to a generic dot.
  std::string icon;
};

/// Context handed to ProductUiAdapter::Mount. The adapter calls
/// `crow::SimpleApp::route_dynamic(...)` against `app`, registers
/// SSE topic bindings against `events`, and renders fragments
/// through `templates`.
struct AdapterContext {
  crow::SimpleApp *app;
  const render::TemplateEngine *templates;
  EventStream *events;
};

/// Convert one NavEntry to its JSON representation as expected by
/// the layout template (`{href, label, slug, icon}`).
/// @param entry The nav entry.
/// @returns JSON object.
auto ToJson(const NavEntry &entry) -> nlohmann::json;

/// Convert a list of NavEntry to a JSON array. Adapter route
/// handlers call this and assign the result to `args.meta["nav"]`,
/// avoiding a hand-rolled loop at every call site.
/// @param entries Nav entries.
/// @returns JSON array.
auto NavToJson(const std::vector<NavEntry> &entries) -> nlohmann::json;

/// Adapter contract. Inheritance is the right call here: an adapter
/// is a cross-module contract, exactly like cli's ProductAdapter.
class ProductUiAdapter {
 public:
  virtual ~ProductUiAdapter() = default;

  /// Stable adapter slug (e.g. "f", "hd-relay"). Surfaced in URLs
  /// and metric labels. Must match `^[a-z][a-z0-9-]*$`.
  virtual auto Slug() const -> std::string = 0;

  /// Human-readable name shown in the page title and nav brand.
  virtual auto DisplayName() const -> std::string = 0;

  /// Filesystem path to this adapter's templates directory. The
  /// framework prepends this to the template engine's search path
  /// at startup so adapter templates can `include` framework
  /// partials, and adapter templates with the same name as a
  /// framework partial override it.
  virtual auto TemplatesDir() const -> std::string = 0;

  /// Top-level nav entries this adapter contributes. Order is
  /// preserved in the rendered nav.
  virtual auto Nav() const -> std::vector<NavEntry> = 0;

  /// Register routes and SSE topic bindings against `ctx`. Called
  /// once at startup, after the framework has applied its defaults.
  virtual auto Mount(AdapterContext ctx) -> void = 0;
};

}  // namespace einheit::ui

#endif  // INCLUDE_EINHEIT_UI_ADAPTER_H_
