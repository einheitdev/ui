/// @file ui_adapter.h
/// @brief Example product adapter for einheit-ui. Mirrors the role
/// of cli's adapters/example: a minimal, working implementation
/// that other adapters can copy.
// Copyright (c) 2026 Einheit Networks

#ifndef INCLUDE_EINHEIT_ADAPTERS_EXAMPLE_UI_ADAPTER_H_
#define INCLUDE_EINHEIT_ADAPTERS_EXAMPLE_UI_ADAPTER_H_

#include <memory>

#include "einheit/ui/adapter.h"

namespace einheit::adapters::example {

/// Construct the example adapter.
/// @returns Owning pointer to a fresh ProductUiAdapter.
auto NewExampleUiAdapter() -> std::unique_ptr<ui::ProductUiAdapter>;

}  // namespace einheit::adapters::example

#endif  // INCLUDE_EINHEIT_ADAPTERS_EXAMPLE_UI_ADAPTER_H_
