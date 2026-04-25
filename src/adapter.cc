/// @file adapter.cc
/// @brief NavEntry JSON helpers. The adapter contract itself is
/// header-only (pure virtuals); this TU just hosts the few small
/// free functions that turn nav structs into the shape the layout
/// template expects.
// Copyright (c) 2026 Einheit Networks

#include "einheit/ui/adapter.h"

namespace einheit::ui {

auto ToJson(const NavEntry &entry) -> nlohmann::json {
  return {
      {"href", entry.href},
      {"label", entry.label},
      {"slug", entry.slug},
      {"icon", entry.icon},
  };
}

auto NavToJson(const std::vector<NavEntry> &entries)
    -> nlohmann::json {
  nlohmann::json out = nlohmann::json::array();
  for (const auto &e : entries) out.push_back(ToJson(e));
  return out;
}

}  // namespace einheit::ui
