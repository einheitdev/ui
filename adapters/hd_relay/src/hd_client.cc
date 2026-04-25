/// @file hd_client.cc
// Copyright (c) 2026 Einheit Networks

#include "einheit/adapters/hd_relay/hd_client.h"

#include <httplib.h>

#include <chrono>
#include <format>
#include <string>
#include <utility>

namespace einheit::adapters::hd_relay {
namespace {

auto MakeError(HdClientError code, std::string msg)
    -> ui::Error<HdClientError> {
  return ui::Error<HdClientError>{code, std::move(msg)};
}

struct ParsedUrl {
  bool tls = false;
  std::string host;
  int port = 80;
};

/// Cheap URL split: splits scheme://host[:port]/<path-ignored>.
/// httplib's Client wants `scheme://host:port` so we extract that
/// and discard the path.
auto ParseBase(const std::string &url)
    -> std::expected<ParsedUrl, ui::Error<HdClientError>> {
  ParsedUrl out;
  std::string rest;
  if (url.starts_with("https://")) {
    out.tls = true;
    out.port = 443;
    rest = url.substr(8);
  } else if (url.starts_with("http://")) {
    out.tls = false;
    out.port = 80;
    rest = url.substr(7);
  } else {
    return std::unexpected(MakeError(
        HdClientError::BadUrl,
        std::format("base_url '{}' missing http(s):// scheme", url)));
  }
  // Strip path.
  auto slash = rest.find('/');
  if (slash != std::string::npos) rest = rest.substr(0, slash);
  auto colon = rest.find(':');
  if (colon != std::string::npos) {
    try {
      out.port = std::stoi(rest.substr(colon + 1));
    } catch (...) {
      return std::unexpected(MakeError(
          HdClientError::BadUrl,
          std::format("base_url '{}' has unparseable port", url)));
    }
    out.host = rest.substr(0, colon);
  } else {
    out.host = rest;
  }
  if (out.host.empty()) {
    return std::unexpected(MakeError(
        HdClientError::BadUrl,
        std::format("base_url '{}' missing host", url)));
  }
  return out;
}

}  // namespace

namespace {

auto BuildClient(const ParsedUrl &url,
                 std::chrono::milliseconds timeout)
    -> std::unique_ptr<httplib::Client> {
  // The two-arg Client(host, port) takes a bare host (no scheme).
  auto c = std::make_unique<httplib::Client>(url.host, url.port);
  c->set_read_timeout(timeout);
  c->set_connection_timeout(timeout);
  c->set_keep_alive(false);
  c->enable_server_certificate_verification(url.tls);
  return c;
}

}  // namespace

struct HdClient::Impl {
  HdClientConfig cfg;
  ParsedUrl url;
  // Each call constructs a fresh httplib::Client because
  // httplib::Client isn't trivially shareable across threads in
  // older versions and the connect cost is small on localhost.
};

HdClient::HdClient(HdClientConfig cfg)
    : impl_(std::make_unique<Impl>()) {
  impl_->cfg = std::move(cfg);
  if (auto p = ParseBase(impl_->cfg.base_url); p) {
    impl_->url = *p;
  }
  // Bad URLs are surfaced lazily on the first request — easier
  // than threading an error through the constructor.
}

HdClient::~HdClient() = default;
HdClient::HdClient(HdClient &&) noexcept = default;
auto HdClient::operator=(HdClient &&) noexcept -> HdClient & = default;

namespace {

auto Headers(const HdClientConfig &cfg) -> httplib::Headers {
  httplib::Headers h;
  if (!cfg.bearer_token.empty()) {
    h.emplace("Authorization",
              std::format("Bearer {}", cfg.bearer_token));
  }
  return h;
}

auto ParseJson(const std::string &body)
    -> std::expected<nlohmann::json, ui::Error<HdClientError>> {
  if (body.empty()) {
    return nlohmann::json::object();
  }
  try {
    return nlohmann::json::parse(body);
  } catch (const std::exception &e) {
    // hd returns plain text for some 2xx responses ("approved",
    // "denied"). Normalise those to {"message": "..."} so the
    // adapter doesn't have to special-case.
    if (body.size() < 256 && body.find('\n') == std::string::npos &&
        body.front() != '{' && body.front() != '[') {
      nlohmann::json j;
      j["message"] = body;
      return j;
    }
    return std::unexpected(MakeError(
        HdClientError::Json,
        std::format("response body is not JSON: {}", e.what())));
  }
}

}  // namespace

auto HdClient::Get(std::string_view path) const
    -> std::expected<nlohmann::json, ui::Error<HdClientError>> {
  if (impl_->url.host.empty()) {
    return std::unexpected(MakeError(HdClientError::BadUrl,
                                     impl_->cfg.base_url));
  }
  auto cli = BuildClient(impl_->url, impl_->cfg.timeout);
  auto res = cli->Get(std::string{path}, Headers(impl_->cfg));
  if (!res) {
    return std::unexpected(MakeError(
        HdClientError::Network,
        std::format("GET {} failed: {}", path,
                    httplib::to_string(res.error()))));
  }
  if (res->status < 200 || res->status >= 300) {
    return std::unexpected(MakeError(
        HdClientError::HttpError,
        std::format("GET {} returned {}: {}", path, res->status,
                    res->body)));
  }
  return ParseJson(res->body);
}

auto HdClient::Post(std::string_view path, std::string_view body,
                    std::string_view content_type) const
    -> std::expected<nlohmann::json, ui::Error<HdClientError>> {
  if (impl_->url.host.empty()) {
    return std::unexpected(MakeError(HdClientError::BadUrl,
                                     impl_->cfg.base_url));
  }
  auto cli = BuildClient(impl_->url, impl_->cfg.timeout);
  auto res = cli->Post(std::string{path}, Headers(impl_->cfg),
                       std::string{body}, std::string{content_type});
  if (!res) {
    return std::unexpected(MakeError(
        HdClientError::Network,
        std::format("POST {} failed: {}", path,
                    httplib::to_string(res.error()))));
  }
  if (res->status < 200 || res->status >= 300) {
    return std::unexpected(MakeError(
        HdClientError::HttpError,
        std::format("POST {} returned {}: {}", path, res->status,
                    res->body)));
  }
  return ParseJson(res->body);
}

}  // namespace einheit::adapters::hd_relay
