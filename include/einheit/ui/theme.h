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
#include <vector>

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

  // Defaults are the real psychotropic palette — hex values lifted
  // from psychotropic.nvim/lua/psychotropic/colors/palette.lua so
  // the operator UI tracks the editor scheme. Greys come from the
  // nvim grey ramp; semantic accents map to the nvim palette's
  // lime / khaki / red / lightblue / violet.

  /// Page background (deepest). nvim `bg`/`black` (#101010).
  Rgb bg{16, 16, 16};
  /// Card / panel background. nvim `grey11`.
  Rgb bg1{28, 28, 28};
  /// Hover / pressed background. nvim `grey18`.
  Rgb bg2{46, 46, 46};

  /// Primary foreground. nvim `white` (#F6FAFA).
  Rgb fg{246, 250, 250};
  /// Secondary foreground (labels, captions). nvim `grey70`.
  Rgb fg2{178, 178, 178};
  /// Tertiary foreground (placeholder, dim). nvim `grey50`.
  Rgb fg3{128, 128, 128};

  /// Semantic — green: healthy, allowed, active. nvim `lime`.
  Rgb good{153, 218, 61};
  /// Semantic — yellow: degraded, connecting, rate-limited. nvim
  /// `khaki`.
  Rgb warn{249, 203, 82};
  /// Semantic — red: failed, denied, dropped. nvim `red`.
  Rgb bad{255, 84, 84};
  /// Semantic — blue: labels, neutral highlights. nvim
  /// `lightblue`.
  Rgb info{77, 185, 244};
  /// Borders, dividers. nvim `grey23`.
  Rgb border{58, 58, 58};
  /// Brand accent — logo, primary buttons, focus ring. nvim
  /// `violet`. The most psychotropic-feeling colour in the
  /// palette without being neon-magenta-aggressive.
  Rgb accent{224, 97, 249};
};

/// Default truecolor dark palette. Matches the cli "psychotropic".
auto DefaultDark() -> Theme;

/// Default truecolor light palette.
auto DefaultLight() -> Theme;

/// Cool blue-teal theme — mirrors cli's OceanTheme but with a full
/// dark-water bg/fg pair sized for the web.
auto OceanTheme() -> Theme;

/// Earthy greens + moss — mirrors cli's ForestTheme.
auto ForestTheme() -> Theme;

/// Ethan Schoonover's Solarized palette, dark variant.
auto SolarizedDarkTheme() -> Theme;

/// Maximum-legibility near-monochromatic palette. Useful for
/// projectors and operators with reduced colour vision.
auto HighContrastTheme() -> Theme;

/// Look up a named theme. Known names (case-insensitive):
/// psychotropic / psycho / default, light, ocean, forest,
/// solarized / solarized-dark, contrast / high-contrast.
/// @param name Theme name.
/// @returns Populated Theme, or DefaultDark() if name is unknown.
auto NamedTheme(const std::string &name) -> Theme;

/// Every name understood by NamedTheme(), sorted alphabetically.
/// Useful for `--theme` help text and a settings dropdown.
/// @returns Sorted vector of theme names.
auto NamedThemeList() -> std::vector<std::string>;

/// Render the theme as a JSON object suitable for direct interpolation
/// into a CSS template (one key per CSS custom property, value =
/// `#rrggbb`). Keys are kebab-case and unprefixed: the template
/// chooses the `--einheit-` prefix.
/// @param t Theme.
/// @returns Flat JSON object ready for inja.
auto ToJson(const Theme &t) -> nlohmann::json;

}  // namespace einheit::ui

#endif  // INCLUDE_EINHEIT_UI_THEME_H_
