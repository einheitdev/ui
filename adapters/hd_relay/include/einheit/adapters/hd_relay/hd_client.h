/// @file hd_client.h
/// @brief HTTP client for the Hyper-DERP daemon's `/api/v1/...`
/// endpoints. Wraps cpp-httplib so the adapter never sees the raw
/// HTTP library and tests can substitute a fake by depending on
/// the same `std::expected<json, Error>` shape.
// Copyright (c) 2026 Einheit Networks

#ifndef INCLUDE_EINHEIT_ADAPTERS_HD_RELAY_HD_CLIENT_H_
#define INCLUDE_EINHEIT_ADAPTERS_HD_RELAY_HD_CLIENT_H_

#include <chrono>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "einheit/ui/error.h"

namespace einheit::adapters::hd_relay {

/// Errors surfaced by the hd HTTP client.
enum class HdClientError {
  /// TCP connect, read, write, or timeout failed.
  Network,
  /// Daemon returned a non-2xx HTTP status.
  HttpError,
  /// Response body was not valid JSON.
  Json,
  /// URL configured at construction time was unparseable.
  BadUrl,
};

/// Settings for an HdClient instance.
struct HdClientConfig {
  /// Base URL of the daemon's metrics endpoint, including scheme
  /// and port. Path component is ignored.
  std::string base_url = "http://127.0.0.1:9090";
  /// Per-request timeout. Applied to connect + read.
  std::chrono::milliseconds timeout{2000};
  /// Optional bearer token for `Authorization`. Empty disables.
  std::string bearer_token;
};

/// Thin HTTP client. Stateless other than holding the configured
/// URL + timeout. Get/Post return parsed JSON or a typed error.
class HdClient {
 public:
  /// @param cfg Client configuration.
  explicit HdClient(HdClientConfig cfg);
  ~HdClient();

  HdClient(const HdClient &) = delete;
  auto operator=(const HdClient &) -> HdClient & = delete;
  HdClient(HdClient &&) noexcept;
  auto operator=(HdClient &&) noexcept -> HdClient &;

  /// GET `path` (relative to base_url) and parse the response as
  /// JSON. Adds the bearer token if configured.
  /// @param path Path component, must start with '/'.
  /// @returns Parsed JSON or HdClientError.
  auto Get(std::string_view path) const
      -> std::expected<nlohmann::json, ui::Error<HdClientError>>;

  /// POST `path` with an optional body. Empty body POSTs are
  /// common for hd's "approve / deny / clear-policy" endpoints.
  /// @param path Path component.
  /// @param body Request body (raw bytes).
  /// @param content_type Content-Type header for non-empty bodies.
  /// @returns Parsed JSON or HdClientError. Bodies that aren't
  /// JSON (e.g. plain "approved") return an empty object on 2xx.
  auto Post(std::string_view path, std::string_view body = "",
            std::string_view content_type = "application/json") const
      -> std::expected<nlohmann::json, ui::Error<HdClientError>>;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace einheit::adapters::hd_relay

#endif  // INCLUDE_EINHEIT_ADAPTERS_HD_RELAY_HD_CLIENT_H_
