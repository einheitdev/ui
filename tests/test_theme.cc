/// @file test_theme.cc
/// @brief Theme palette factories, named-theme lookup, and JSON
/// serialisation used to drive the CSS template.
// Copyright (c) 2026 Einheit Networks

#include <algorithm>
#include <iterator>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "einheit/ui/theme.h"

namespace einheit::ui {

TEST(Theme, ToHexBoundaries) {
  EXPECT_EQ(ToHex(Rgb{0, 0, 0}), "#000000");
  EXPECT_EQ(ToHex(Rgb{255, 255, 255}), "#ffffff");
  EXPECT_EQ(ToHex(Rgb{0x12, 0x34, 0x56}), "#123456");
}

TEST(Theme, DefaultDarkAndLightDiffer) {
  const auto dark = DefaultDark();
  const auto light = DefaultLight();
  EXPECT_NE(ToHex(dark.bg), ToHex(light.bg));
  EXPECT_NE(ToHex(dark.fg), ToHex(light.fg));
}

TEST(Theme, NamedThemeLookupCaseInsensitive) {
  const auto a = NamedTheme("light");
  const auto b = NamedTheme("LIGHT");
  const auto c = NamedTheme("Light");
  EXPECT_EQ(ToHex(a.bg), ToHex(b.bg));
  EXPECT_EQ(ToHex(a.bg), ToHex(c.bg));
}

TEST(Theme, NamedThemeUnknownFallsBackToDark) {
  const auto fallback = NamedTheme("definitely-not-a-theme");
  const auto dark = DefaultDark();
  EXPECT_EQ(ToHex(fallback.bg), ToHex(dark.bg));
}

TEST(Theme, ToJsonHasAllSemanticKeys) {
  const auto j = ToJson(DefaultDark());
  for (const auto *key :
       {"name", "bg", "bg1", "bg2", "fg", "fg2", "fg3", "good",
        "warn", "bad", "info", "border", "accent"}) {
    EXPECT_TRUE(j.contains(key)) << "missing key: " << key;
  }
}

TEST(Theme, ToJsonValuesAreHexLiterals) {
  const auto j = ToJson(DefaultDark());
  for (const auto *key :
       {"bg", "bg1", "bg2", "fg", "fg2", "fg3", "good", "warn",
        "bad", "info", "border", "accent"}) {
    const auto v = j.at(key).get<std::string>();
    ASSERT_EQ(v.size(), 7u) << key << " = " << v;
    EXPECT_EQ(v[0], '#') << key;
  }
}

TEST(Theme, NameRoundTripsThroughJson) {
  Theme t;
  t.name = "ocean";
  const auto j = ToJson(t);
  EXPECT_EQ(j.at("name").get<std::string>(), "ocean");
}

TEST(Theme, AllNamedThemesAreDistinct) {
  // Different themes shouldn't collide on the page background or
  // accent — that's a quick proxy for "actually different palette".
  const Theme dark = DefaultDark();
  const Theme light = DefaultLight();
  const Theme ocean = OceanTheme();
  const Theme forest = ForestTheme();
  const Theme solarized = SolarizedDarkTheme();
  const Theme hc = HighContrastTheme();

  // Pairwise: every (a,b) differs in at least the bg or the accent.
  const Theme themes[] = {dark,      light,     ocean,
                          forest,    solarized, hc};
  for (std::size_t i = 0; i < std::size(themes); ++i) {
    for (std::size_t j = i + 1; j < std::size(themes); ++j) {
      const bool same =
          (ToHex(themes[i].bg) == ToHex(themes[j].bg)) &&
          (ToHex(themes[i].accent) == ToHex(themes[j].accent));
      EXPECT_FALSE(same)
          << themes[i].name << " collides with " << themes[j].name;
    }
  }
}

TEST(Theme, NamedThemeListContainsExpectedNames) {
  const auto list = NamedThemeList();
  for (const auto *expected :
       {"forest", "high-contrast", "light", "ocean",
        "psychotropic", "solarized-dark"}) {
    EXPECT_NE(std::find(list.begin(), list.end(), expected),
              list.end())
        << "missing: " << expected;
  }
}

TEST(Theme, NamedThemeAcceptsAliases) {
  // Both spellings are documented.
  EXPECT_EQ(ToHex(NamedTheme("solarized").bg),
            ToHex(NamedTheme("solarized-dark").bg));
  EXPECT_EQ(ToHex(NamedTheme("contrast").bg),
            ToHex(NamedTheme("high-contrast").bg));
}

TEST(Theme, NamedThemeRoutesToCorrectPalette) {
  EXPECT_EQ(ToHex(NamedTheme("ocean").bg), ToHex(OceanTheme().bg));
  EXPECT_EQ(ToHex(NamedTheme("forest").bg), ToHex(ForestTheme().bg));
  EXPECT_EQ(ToHex(NamedTheme("light").bg),
            ToHex(DefaultLight().bg));
}

}  // namespace einheit::ui
