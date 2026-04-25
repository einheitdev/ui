/// @file escape.h
/// @brief HTML / attribute / URL escapers. inja auto-escapes
/// `{{ var }}` interpolation; these helpers exist for the rare path
/// that builds raw strings outside the template engine (event
/// payloads, SSE comment lines, dev-mode error pages).
// Copyright (c) 2026 Einheit Networks

#ifndef INCLUDE_EINHEIT_UI_RENDER_ESCAPE_H_
#define INCLUDE_EINHEIT_UI_RENDER_ESCAPE_H_

#include <string>
#include <string_view>

namespace einheit::ui::render {

/// Escape a string for inclusion in HTML element bodies.
/// Replaces `& < > " '` with their entity references.
/// @param s Untrusted text.
/// @returns Escaped text safe to splice between HTML tags.
auto EscapeHtml(std::string_view s) -> std::string;

/// Escape a string for use inside a double-quoted HTML attribute.
/// Same as EscapeHtml but additionally tightens whitespace handling
/// so `"`, `'`, and control bytes can never break out of the attr.
/// @param s Untrusted text.
/// @returns Escaped attribute value.
auto EscapeAttr(std::string_view s) -> std::string;

/// Percent-encode a string for use in a URL path or query value.
/// Reserves only RFC 3986 unreserved characters; everything else is
/// `%HH`-encoded.
/// @param s Untrusted text.
/// @returns URL-safe encoding.
auto EscapeUrl(std::string_view s) -> std::string;

}  // namespace einheit::ui::render

#endif  // INCLUDE_EINHEIT_UI_RENDER_ESCAPE_H_
