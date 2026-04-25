/// @file theme.h
/// @brief Theme primitives for the web layer. Mirrors the semantic
/// palette in einheit/cli/render/theme.h so a product's `Good` /
/// `Warn` / `Bad` cells render the same colour in the terminal and
/// the browser. Themes serialise to a CSS custom-property block that
/// the layout template includes.
// Copyright (c) 2026 Einheit Networks

#ifndef INCLUDE_EINHEIT_UI_THEME_H_
#define INCLUDE_EINHEIT_UI_THEME_H_

#include <array>
#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace einheit::ui {

/// 24-bit RGB triple. Stored as bytes; rendered as `#rrggbb`.
struct Rgb {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
};

/// Format a Rgb as a `#rrggbb` literal.
/// @param c Colour.
/// @returns Hex literal.
auto ToHex(Rgb c) -> std::string;

/// Semantic-named palette. Keep in lockstep with the cli Theme: a
/// product reads its data once and renders it in either layer.
struct Theme {
  /// Display name shown in dev tools and operator UI.
  std::string name = "psychotropic";

  /// Page background (deepest).
  Rgb bg{15, 23, 42};
  /// Card / panel background.
  Rgb bg1{30, 41, 59};
  /// Hover / pressed background.
  Rgb bg2{51, 65, 85};

  /// Primary foreground.
  Rgb fg{226, 232, 240};
  /// Secondary foreground (labels, captions).
  Rgb fg2{148, 163, 184};
  /// Tertiary foreground (placeholder, dim).
  Rgb fg3{100, 116, 139};

  /// Semantic — green: healthy, allowed, active.
  Rgb good{34, 197, 94};
  /// Semantic — yellow: degraded, connecting, rate-limited.
  Rgb warn{234, 179, 8};
  /// Semantic — red: failed, denied, dropped.
  Rgb bad{239, 68, 68};
  /// Semantic — cyan: labels, neutral highlights.
  Rgb info{56, 189, 248};
  /// Borders, dividers.
  Rgb border{51, 65, 85};
  /// Brand accent — logo, primary buttons, focus ring.
  Rgb accent{56, 189, 248};
};

/// Default truecolor dark palette. Matches the cli "psychotropic".
auto DefaultDark() -> Theme;

/// Default truecolor light palette.
auto DefaultLight() -> Theme;

/// Look up a named theme. Known names: psychotropic, ocean, forest,
/// solarized-dark, high-contrast, light.
/// @param name Theme name (case-insensitive).
/// @returns Populated Theme, or DefaultDark() if name is unknown.
auto NamedTheme(const std::string &name) -> Theme;

/// Render the theme as a JSON object suitable for direct interpolation
/// into a CSS template (one key per CSS custom property, value =
/// `#rrggbb`). Keys are kebab-case and unprefixed: the template
/// chooses the `--einheit-` prefix.
/// @param t Theme.
/// @returns Flat JSON object ready for inja.
auto ToJson(const Theme &t) -> nlohmann::json;

}  // namespace einheit::ui

#endif  // INCLUDE_EINHEIT_UI_THEME_H_
