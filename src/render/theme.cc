/// @file theme.cc
// Copyright (c) 2026 Einheit Networks

#include "einheit/ui/theme.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace einheit::ui {

auto ToHex(Rgb c) -> std::string {
  return std::format("#{:02x}{:02x}{:02x}", c.r, c.g, c.b);
}

auto DefaultDark() -> Theme {
  // Stays in sync with cli/render/theme.cc DefaultDarkTrueColor().
  return Theme{};
}

auto DefaultLight() -> Theme {
  Theme t;
  t.name = "light";
  t.bg = {248, 250, 252};
  t.bg1 = {241, 245, 249};
  t.bg2 = {226, 232, 240};
  t.fg = {15, 23, 42};
  t.fg2 = {51, 65, 85};
  t.fg3 = {100, 116, 139};
  t.border = {203, 213, 225};
  // Semantic colours stay in the same hue family but darker for
  // legibility on a light background.
  t.good = {21, 128, 61};
  t.warn = {161, 98, 7};
  t.bad = {185, 28, 28};
  t.info = {2, 132, 199};
  t.accent = {2, 132, 199};
  return t;
}

namespace {

auto ToLower(std::string s) -> std::string {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

}  // namespace

auto OceanTheme() -> Theme {
  Theme t;
  t.name = "ocean";
  t.bg = {10, 22, 40};
  t.bg1 = {15, 34, 53};
  t.bg2 = {26, 49, 72};
  t.fg = {216, 232, 244};
  t.fg2 = {136, 165, 190};
  t.fg3 = {74, 100, 120};
  t.border = {42, 58, 74};
  t.accent = {63, 169, 245};
  t.good = {78, 201, 176};
  t.warn = {245, 201, 113};
  t.bad = {226, 109, 109};
  t.info = {107, 182, 255};
  return t;
}

auto ForestTheme() -> Theme {
  Theme t;
  t.name = "forest";
  t.bg = {26, 31, 21};
  t.bg1 = {35, 42, 28};
  t.bg2 = {47, 56, 38};
  t.fg = {234, 230, 216};
  t.fg2 = {184, 178, 154};
  t.fg3 = {106, 114, 90};
  t.border = {56, 64, 46};
  t.accent = {189, 211, 100};
  t.good = {133, 196, 108};
  t.warn = {212, 168, 75};
  t.bad = {203, 101, 101};
  t.info = {127, 184, 118};
  return t;
}

auto SolarizedDarkTheme() -> Theme {
  Theme t;
  t.name = "solarized-dark";
  // Solarized base03/02/01/00/0/1, semantic from canonical
  // Solarized accents.
  t.bg = {0, 43, 54};
  t.bg1 = {7, 54, 66};
  t.bg2 = {88, 110, 117};
  t.fg = {147, 161, 161};
  t.fg2 = {131, 148, 150};
  t.fg3 = {101, 123, 131};
  t.border = {7, 54, 66};
  t.accent = {108, 113, 196};
  t.good = {133, 153, 0};
  t.warn = {181, 137, 0};
  t.bad = {220, 50, 47};
  t.info = {38, 139, 210};
  return t;
}

auto HighContrastTheme() -> Theme {
  Theme t;
  t.name = "high-contrast";
  t.bg = {0, 0, 0};
  t.bg1 = {26, 26, 26};
  t.bg2 = {42, 42, 42};
  t.fg = {255, 255, 255};
  t.fg2 = {224, 224, 224};
  t.fg3 = {160, 160, 160};
  t.border = {128, 128, 128};
  t.accent = {255, 255, 255};
  t.good = {0, 255, 0};
  t.warn = {255, 215, 0};
  t.bad = {255, 107, 107};
  t.info = {0, 255, 255};
  return t;
}

auto NamedTheme(const std::string &name) -> Theme {
  const auto n = ToLower(name);
  if (n == "light") return DefaultLight();
  if (n == "ocean") return OceanTheme();
  if (n == "forest") return ForestTheme();
  if (n == "solarized" || n == "solarized-dark") {
    return SolarizedDarkTheme();
  }
  if (n == "contrast" || n == "high-contrast") {
    return HighContrastTheme();
  }
  // Unknown / "psychotropic" / "psycho" / "default" → dark.
  return DefaultDark();
}

auto NamedThemeList() -> std::vector<std::string> {
  return {"forest",         "high-contrast", "light",
          "ocean",          "psychotropic",  "solarized-dark"};
}

auto ToJson(const Theme &t) -> nlohmann::json {
  nlohmann::json j;
  j["name"] = t.name;
  j["bg"] = ToHex(t.bg);
  j["bg1"] = ToHex(t.bg1);
  j["bg2"] = ToHex(t.bg2);
  j["fg"] = ToHex(t.fg);
  j["fg2"] = ToHex(t.fg2);
  j["fg3"] = ToHex(t.fg3);
  j["good"] = ToHex(t.good);
  j["warn"] = ToHex(t.warn);
  j["bad"] = ToHex(t.bad);
  j["info"] = ToHex(t.info);
  j["border"] = ToHex(t.border);
  j["accent"] = ToHex(t.accent);
  return j;
}

}  // namespace einheit::ui
