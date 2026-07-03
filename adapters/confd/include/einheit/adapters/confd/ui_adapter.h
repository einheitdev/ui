/// @file ui_adapter.h
/// @brief confd product adapter for einheit-ui — the reference
/// implementation of "the UI drives the CLI command engine".
///
/// Unlike the example adapter (which mutates its own in-process counter
/// directly), every *mutating* action here goes through the shared
/// command engine via `ui::CommandDriver`: a button becomes a confd
/// verb (configure / set / commit / commit-confirmed / confirm /
/// rollback), executed against a framework `confd::Runtime` over a
/// `MemoryBackend` (a fake programmable box). Auth gating and audit
/// logging happen once, in the engine — so a mutation from this UI is
/// indistinguishable, in the audit trail, from the same command typed
/// in the CLI. Reads (running config + the commit-confirmed countdown)
/// stay on the fast direct path straight off the Runtime.
// Copyright (c) 2026 Einheit Networks

#ifndef INCLUDE_EINHEIT_ADAPTERS_CONFD_UI_ADAPTER_H_
#define INCLUDE_EINHEIT_ADAPTERS_CONFD_UI_ADAPTER_H_

#include <memory>
#include <string>

#include "einheit/ui/adapter.h"

namespace einheit::adapters::confd {

/// Construction options for the confd adapter.
struct ConfdConfig {
  /// Directory for durable confd state (running config + history +
  /// pending commit-confirm). Empty means in-memory only.
  std::string state_dir;
};

/// Construct the confd reference adapter. It owns a MemoryBackend, a
/// confd Runtime, an in-process transport, and a CommandDriver over
/// them, so the adapter is a self-contained single-binary appliance.
/// @param cfg Adapter options.
/// @returns Owning pointer to a fresh ProductUiAdapter.
auto NewConfdUiAdapter(ConfdConfig cfg = {})
    -> std::unique_ptr<ui::ProductUiAdapter>;

}  // namespace einheit::adapters::confd

#endif  // INCLUDE_EINHEIT_ADAPTERS_CONFD_UI_ADAPTER_H_
