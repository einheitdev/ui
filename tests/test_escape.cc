/// @file test_escape.cc
/// @brief Exercises HTML / attribute / URL escapers. inja
/// auto-escapes interpolation, but the helpers themselves cover the
/// hand-built paths (SSE comments, dev error pages) and need to be
/// correct on their own.
// Copyright (c) 2026 Einheit Networks

#include <string>

#include <gtest/gtest.h>

#include "einheit/ui/render/escape.h"

namespace einheit::ui::render {

TEST(EscapeHtml, ReplacesFiveSpecialChars) {
  EXPECT_EQ(EscapeHtml("a&b"), "a&amp;b");
  EXPECT_EQ(EscapeHtml("a<b"), "a&lt;b");
  EXPECT_EQ(EscapeHtml("a>b"), "a&gt;b");
  EXPECT_EQ(EscapeHtml(R"(a"b)"), "a&quot;b");
  EXPECT_EQ(EscapeHtml("a'b"), "a&#39;b");
}

TEST(EscapeHtml, PreservesPlainText) {
  EXPECT_EQ(EscapeHtml("hello world"), "hello world");
  EXPECT_EQ(EscapeHtml("munich/berlin"), "munich/berlin");
  EXPECT_EQ(EscapeHtml(""), "");
}

TEST(EscapeHtml, HandlesScriptInjection) {
  const auto escaped =
      EscapeHtml("<script>alert('x')</script>");
  EXPECT_EQ(escaped.find("<script>"), std::string::npos);
  EXPECT_NE(escaped.find("&lt;script&gt;"), std::string::npos);
}

TEST(EscapeAttr, EncodesControlBytes) {
  // Carriage return / form feed must not survive into an attribute.
  const auto escaped = EscapeAttr(std::string{"a\rb\fc"});
  EXPECT_EQ(escaped.find('\r'), std::string::npos);
  EXPECT_EQ(escaped.find('\f'), std::string::npos);
  EXPECT_NE(escaped.find("&#13;"), std::string::npos);
  EXPECT_NE(escaped.find("&#12;"), std::string::npos);
}

TEST(EscapeAttr, KeepsTab) {
  // Tab is the one control byte that's allowed through verbatim.
  const auto escaped = EscapeAttr(std::string{"a\tb"});
  EXPECT_NE(escaped.find('\t'), std::string::npos);
}

TEST(EscapeUrl, AlphanumericPassthrough) {
  EXPECT_EQ(EscapeUrl("abcXYZ012"), "abcXYZ012");
}

TEST(EscapeUrl, UnreservedPassthrough) {
  // RFC 3986 unreserved set: A-Z a-z 0-9 - _ . ~
  EXPECT_EQ(EscapeUrl("a-b_c.d~e"), "a-b_c.d~e");
}

TEST(EscapeUrl, PercentEncodesSpaceAndSlash) {
  EXPECT_EQ(EscapeUrl("a b"), "a%20b");
  EXPECT_EQ(EscapeUrl("a/b"), "a%2Fb");
  EXPECT_EQ(EscapeUrl("a&b"), "a%26b");
  EXPECT_EQ(EscapeUrl("="), "%3D");
}

TEST(EscapeUrl, UpperCaseHexDigits) {
  // RFC 3986 §2.1: producers should use uppercase hex.
  const auto out = EscapeUrl("\xff");
  EXPECT_EQ(out, "%FF");
}

}  // namespace einheit::ui::render
