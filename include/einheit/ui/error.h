/// @file error.h
/// @brief Shared Error<E> wrapper paired with std::expected. Mirrors
/// einheit/cli/error.h so error handling is uniform across the
/// terminal and web framework layers.
// Copyright (c) 2026 Einheit Networks

#ifndef INCLUDE_EINHEIT_UI_ERROR_H_
#define INCLUDE_EINHEIT_UI_ERROR_H_

#include <string>

namespace einheit::ui {

/// Uniform error wrapper: a typed enum plus a human-readable message.
/// @tparam ErrorCodeEnum A strongly-typed enum declared per module
/// (e.g. TemplateError, RouteError, StreamError).
template <typename ErrorCodeEnum>
struct Error {
  /// Machine-readable code. Callers branch on this.
  ErrorCodeEnum code;
  /// Human-readable context. Logged and surfaced in dev pages.
  std::string message;
};

}  // namespace einheit::ui

#endif  // INCLUDE_EINHEIT_UI_ERROR_H_
