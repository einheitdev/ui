/// @file test_theme.cc
/// @brief Theme palette factories, named-theme lookup, and JSON
/// serialisation used to drive the CSS template.
// Copyright (c) 2026 Einheit Networks

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

}  // namespace einheit::ui
