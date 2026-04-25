/// @file escape.cc
// Copyright (c) 2026 Einheit Networks

#include "einheit/ui/render/escape.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace einheit::ui::render {
namespace {

constexpr auto IsUnreserved(unsigned char c) -> bool {
  return std::isalnum(c) || c == '-' || c == '_' || c == '.' ||
         c == '~';
}

constexpr std::array<char, 16> kHex{
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

}  // namespace

auto EscapeHtml(std::string_view s) -> std::string {
  std::string out;
  out.reserve(s.size());
  for (char ch : s) {
    switch (ch) {
      case '&':  out += "&amp;";  break;
      case '<':  out += "&lt;";   break;
      case '>':  out += "&gt;";   break;
      case '"':  out += "&quot;"; break;
      case '\'': out += "&#39;";  break;
      default:   out += ch;       break;
    }
  }
  return out;
}

auto EscapeAttr(std::string_view s) -> std::string {
  std::string out;
  out.reserve(s.size());
  for (char ch : s) {
    auto c = static_cast<unsigned char>(ch);
    if (c < 0x20 && c != '\t') {
      out += "&#";
      out += std::to_string(static_cast<unsigned>(c));
      out += ';';
      continue;
    }
    switch (ch) {
      case '&':  out += "&amp;";  break;
      case '<':  out += "&lt;";   break;
      case '>':  out += "&gt;";   break;
      case '"':  out += "&quot;"; break;
      case '\'': out += "&#39;";  break;
      default:   out += ch;       break;
    }
  }
  return out;
}

auto EscapeUrl(std::string_view s) -> std::string {
  std::string out;
  out.reserve(s.size());
  for (char ch : s) {
    auto c = static_cast<unsigned char>(ch);
    if (IsUnreserved(c)) {
      out += ch;
    } else {
      out += '%';
      out += kHex[(c >> 4) & 0xF];
      out += kHex[c & 0xF];
    }
  }
  return out;
}

}  // namespace einheit::ui::render
