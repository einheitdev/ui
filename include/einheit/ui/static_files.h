/// @file static_files.h
/// @brief Tiny static-asset handler. Crow has nothing built-in we
/// trust for production (path traversal, MIME, etag), so the
/// framework owns its own mount with a small allowed-suffix list.
// Copyright (c) 2026 Einheit Networks

#ifndef INCLUDE_EINHEIT_UI_STATIC_FILES_H_
#define INCLUDE_EINHEIT_UI_STATIC_FILES_H_

#include <string>
#include <string_view>

#include <crow.h>

namespace einheit::ui {

/// Mount `dir` on `app` at the URL prefix `mount_at`. Refuses path
/// traversal (`..`), follows symlinks only when their realpath is
/// inside `dir`, and serves files with a small MIME table covering
/// js/css/svg/woff2/png/ico. Sets immutable Cache-Control on
/// fingerprinted asset names (`*.<hash>.js`).
/// @param app Crow app.
/// @param mount_at URL prefix, e.g. "/assets".
/// @param dir Filesystem directory to serve from.
auto MountStatic(crow::SimpleApp &app, std::string_view mount_at,
                 std::string_view dir) -> void;

}  // namespace einheit::ui

#endif  // INCLUDE_EINHEIT_UI_STATIC_FILES_H_
