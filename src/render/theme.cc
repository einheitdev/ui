/// @file theme.cc
// Copyright (c) 2026 Einheit Networks

#include "einheit/ui/theme.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <string>

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

auto NamedTheme(const std::string &name) -> Theme {
  const auto n = ToLower(name);
  if (n == "light") return DefaultLight();
  // Additional named themes will land here as they're ported from
  // cli/render/theme.cc. Falling back to dark keeps unknown names
  // safe rather than failing a render.
  return DefaultDark();
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
