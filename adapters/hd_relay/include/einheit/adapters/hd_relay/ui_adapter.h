/// @file ui_adapter.h
/// @brief Hyper-DERP UI adapter for einheit-ui. Renders the
/// daemon's `/api/v1/...` JSON surface as the operator dashboard.
// Copyright (c) 2026 Einheit Networks

#ifndef INCLUDE_EINHEIT_ADAPTERS_HD_RELAY_UI_ADAPTER_H_
#define INCLUDE_EINHEIT_ADAPTERS_HD_RELAY_UI_ADAPTER_H_

#include <memory>

#include "einheit/adapters/hd_relay/hd_client.h"
#include "einheit/ui/adapter.h"

namespace einheit::adapters::hd_relay {

/// Construct the hd-relay UI adapter pointed at a running daemon.
/// @param cfg HTTP client configuration; specifies base URL + auth.
/// @returns Owning pointer to a fresh ProductUiAdapter.
auto NewHdRelayUiAdapter(HdClientConfig cfg)
    -> std::unique_ptr<ui::ProductUiAdapter>;

}  // namespace einheit::adapters::hd_relay

#endif  // INCLUDE_EINHEIT_ADAPTERS_HD_RELAY_UI_ADAPTER_H_
