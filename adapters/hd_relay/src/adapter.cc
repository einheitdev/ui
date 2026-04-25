/// @file adapter.cc
/// @brief hd-relay UI adapter. Reads JSON from hd's `/api/v1/...`
/// endpoints, renders fragments via the framework partials,
/// proxies action POSTs (approve, deny) and returns the updated
/// row for HTMX to swap.
// Copyright (c) 2026 Einheit Networks

#include "einheit/adapters/hd_relay/ui_adapter.h"

#include <format>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "einheit/ui/route.h"

namespace einheit::adapters::hd_relay {
namespace {

/// Build a partials/status `rows` array out of `/api/v1/relay`'s
/// response body.
auto RelayStatusRows(const nlohmann::json &j) -> nlohmann::json {
  nlohmann::json rows = nlohmann::json::array();
  const bool enabled = j.value("hd_enabled", false);
  rows.push_back({{"label", "hd"},
                  {"value", enabled ? "enabled" : "disabled"},
                  {"semantic", enabled ? "good" : "warn"}});
  if (enabled) {
    rows.push_back(
        {{"label", "peers"},
         {"value", std::to_string(j.value("hd_peer_count", 0))}});
    rows.push_back(
        {{"label", "enroll"},
         {"value", j.value("hd_enroll_mode", std::string{})}});
    rows.push_back(
        {{"label", "relay id"},
         {"value", std::to_string(j.value("relay_id", 0))}});
    rows.push_back(
        {{"label", "denylist"},
         {"value",
          std::to_string(j.value("denylist_size", 0))}});
  }
  rows.push_back(
      {{"label", "workers"},
       {"value", std::to_string(j.value("workers", 0))}});
  return rows;
}

/// Build the peers table context (rows of {key, state, rules,
/// actions}) from `/api/v1/peers`'s response body. Each row is a
/// JSON object the peers template iterates.
auto PeersRows(const nlohmann::json &j) -> nlohmann::json {
  nlohmann::json rows = nlohmann::json::array();
  if (!j.contains("peers")) return rows;
  for (const auto &p : j.at("peers")) {
    nlohmann::json row;
    row["key_str"] = p.value("key_str", std::string{});
    row["key"] = p.value("key", std::string{});
    const auto state = p.value("state", std::string{"unknown"});
    row["state"] = state;
    row["state_semantic"] =
        state == "approved"
            ? "good"
            : state == "pending" ? "warn" : "bad";
    row["rule_count"] = p.value("rule_count", 0);
    row["fd"] = p.value("fd", -1);
    rows.push_back(std::move(row));
  }
  return rows;
}

/// Counters card context — total RX/TX bytes plus per-worker
/// breakdown. Bytes are formatted server-side for readability.
auto CountersContext(const nlohmann::json &j) -> nlohmann::json {
  auto fmt_bytes = [](std::int64_t b) -> std::string {
    if (b < 1024) return std::format("{} B", b);
    if (b < 1024L * 1024) {
      return std::format("{:.1f} KiB", b / 1024.0);
    }
    if (b < 1024L * 1024 * 1024) {
      return std::format("{:.1f} MiB", b / (1024.0 * 1024.0));
    }
    return std::format("{:.2f} GiB",
                       b / (1024.0 * 1024.0 * 1024.0));
  };
  nlohmann::json totals = nlohmann::json::array();
  totals.push_back(
      {{"label", "total RX"},
       {"value",
        fmt_bytes(j.value("total_recv_bytes", std::int64_t{0}))}});
  totals.push_back(
      {{"label", "total TX"},
       {"value",
        fmt_bytes(j.value("total_send_bytes", std::int64_t{0}))}});
  const auto drops = j.value("total_send_drops", std::int64_t{0});
  totals.push_back({{"label", "send drops"},
                    {"value", std::to_string(drops)},
                    {"semantic", drops > 0 ? "warn" : "good"}});
  totals.push_back(
      {{"label", "mesh fwds"},
       {"value", std::to_string(j.value(
                     "total_hd_mesh_forwards", std::int64_t{0}))}});
  totals.push_back(
      {{"label", "fleet fwds"},
       {"value",
        std::to_string(j.value("total_hd_fleet_forwards",
                                std::int64_t{0}))}});
  if (j.contains("hd_enrollments")) {
    totals.push_back(
        {{"label", "enrollments"},
         {"value", std::to_string(j.value(
                       "hd_enrollments", std::int64_t{0}))}});
  }
  if (j.contains("hd_auth_failures")) {
    const auto v =
        j.value("hd_auth_failures", std::int64_t{0});
    totals.push_back({{"label", "auth fails"},
                      {"value", std::to_string(v)},
                      {"semantic", v > 0 ? "bad" : "good"}});
  }
  nlohmann::json workers = nlohmann::json::array();
  if (j.contains("workers")) {
    for (const auto &w : j.at("workers")) {
      nlohmann::json wr = nlohmann::json::array();
      wr.push_back({{"text", std::to_string(w.value("id", 0))}});
      wr.push_back(
          {{"text", fmt_bytes(w.value("recv_bytes",
                                      std::int64_t{0}))},
           {"align", "right"}});
      wr.push_back(
          {{"text", fmt_bytes(w.value("send_bytes",
                                      std::int64_t{0}))},
           {"align", "right"}});
      const auto wd = w.value("send_drops", std::int64_t{0});
      wr.push_back(
          {{"text", std::to_string(wd)},
           {"align", "right"},
           {"semantic", wd > 0 ? "warn" : "default"}});
      workers.push_back(std::move(wr));
    }
  }
  nlohmann::json out;
  out["totals"] = std::move(totals);
  out["workers"] = std::move(workers);
  return out;
}

/// Render the audit log as `partials/log_entry` `entries`.
/// hd's audit records are heterogeneous — we surface decision +
/// reason + the involved keys.
auto AuditEntries(const nlohmann::json &j) -> nlohmann::json {
  nlohmann::json entries = nlohmann::json::array();
  if (!j.contains("records")) return entries;
  for (const auto &r : j.at("records")) {
    const auto decision = r.value("decision", std::string{});
    std::string level = "INFO";
    if (decision == "deny" || decision == "rejected") {
      level = "WARNING";
    }
    const auto ts = r.value("ts", r.value("timestamp", std::string{}));
    const auto reason = r.value("reason", std::string{});
    const auto client = r.value("client_key_str",
                                r.value("client", std::string{}));
    const auto target = r.value("target_key_str",
                                r.value("target", std::string{}));
    auto msg = std::format("{} {} → {}{}", decision, client, target,
                            reason.empty()
                                ? std::string{}
                                : std::format(" ({})", reason));
    entries.push_back({{"timestamp", ts},
                       {"level", level},
                       {"message", std::move(msg)}});
  }
  return entries;
}

class HdRelayUiAdapter final : public ui::ProductUiAdapter {
 public:
  explicit HdRelayUiAdapter(HdClientConfig cfg)
      : client_(std::move(cfg)) {}

  auto Slug() const -> std::string override { return "hd-relay"; }
  auto DisplayName() const -> std::string override {
    return "hyper-derp";
  }
  auto TemplatesDir() const -> std::string override {
    return EINHEIT_UI_ADAPTER_HD_RELAY_TEMPLATES_DIR;
  }
  auto Nav() const -> std::vector<ui::NavEntry> override {
    return {
        {"/", "Overview", "overview", "monitor"},
        {"/peers", "Peers", "peers", "users"},
        {"/counters", "Counters", "counters", "activity"},
        {"/audit", "Audit", "audit", "file-text"},
    };
  }

  auto Mount(ui::AdapterContext ctx) -> void override {
    auto *eng = ctx.templates;
    auto &app = *ctx.app;
    auto nav = Nav();
    auto add_meta = [nav, this](nlohmann::json &meta,
                                std::string title,
                                std::string active) {
      meta = {{"title", std::move(title)},
              {"brand", DisplayName()},
              {"active", std::move(active)},
              {"nav", ui::NavToJson(nav)}};
    };

    CROW_ROUTE(app, "/")
    ([eng, this, add_meta](const crow::request &req) {
      auto relay = client_.Get("/api/v1/relay");
      if (!relay) {
        return ui::RenderError(*eng, req, 502,
                               "hd_unreachable",
                               relay.error().message,
                               "is the daemon up?");
      }
      ui::RenderArgs args;
      args.fragment = "hd/overview";
      args.layout = "layout";
      args.data = {{"rows", RelayStatusRows(*relay)}};
      add_meta(args.meta, "hyper-derp — overview", "overview");
      auto r = ui::Render(*eng, req, args);
      if (!r) {
        return ui::RenderError(*eng, req, 500, "render_failed",
                               r.error().message);
      }
      return std::move(*r);
    });

    CROW_ROUTE(app, "/peers")
    ([eng, this, add_meta](const crow::request &req) {
      auto peers = client_.Get("/api/v1/peers");
      if (!peers) {
        return ui::RenderError(*eng, req, 502, "hd_unreachable",
                               peers.error().message);
      }
      ui::RenderArgs args;
      args.fragment = "hd/peers";
      args.layout = "layout";
      args.data = {{"peers", PeersRows(*peers)}};
      add_meta(args.meta, "hyper-derp — peers", "peers");
      auto r = ui::Render(*eng, req, args);
      if (!r) {
        return ui::RenderError(*eng, req, 500, "render_failed",
                               r.error().message);
      }
      return std::move(*r);
    });

    auto peer_action = [this, eng](const crow::request &req,
                                    const std::string &key,
                                    const std::string &verb)
        -> crow::response {
      auto resp = client_.Post(
          std::format("/api/v1/peers/{}/{}", key, verb));
      if (!resp) {
        return ui::RenderError(*eng, req, 502, "hd_action_failed",
                               resp.error().message);
      }
      // Re-fetch the single peer so we can swap its row in place.
      auto peers = client_.Get("/api/v1/peers");
      if (!peers) {
        return ui::RenderError(*eng, req, 502, "hd_unreachable",
                               peers.error().message);
      }
      auto rows = PeersRows(*peers);
      nlohmann::json target;
      for (const auto &row : rows) {
        if (row.value("key", std::string{}) == key) {
          target = row;
          break;
        }
      }
      if (target.is_null()) {
        // Peer disappeared — fall back to a placeholder row that
        // HTMX swaps in. The whole-table re-render at /peers
        // will show the actual final state on the next nav.
        target = {{"key", key},
                  {"key_str", key},
                  {"state", verb == "deny" ? "denied" : "gone"},
                  {"state_semantic", "bad"},
                  {"rule_count", 0},
                  {"fd", -1}};
      }
      // peer_row references its row as `p.*` because that's the
      // loop variable name in hd/peers. Wrap so the same template
      // renders standalone for the POST-action swap.
      ui::RenderArgs args;
      args.fragment = "hd/peer_row";
      args.data = {{"p", std::move(target)}};
      auto r = ui::Render(*eng, ui::ResponseFormat::Fragment, args);
      if (!r) {
        return ui::RenderError(*eng, req, 500, "render_failed",
                               r.error().message);
      }
      return std::move(*r);
    };

    CROW_ROUTE(app, "/peers/<string>/approve")
        .methods("POST"_method)(
            [peer_action](const crow::request &req,
                          std::string key) {
              return peer_action(req, key, "approve");
            });

    CROW_ROUTE(app, "/peers/<string>/deny")
        .methods("POST"_method)(
            [peer_action](const crow::request &req,
                          std::string key) {
              return peer_action(req, key, "deny");
            });

    CROW_ROUTE(app, "/counters")
    ([eng, this, add_meta](const crow::request &req) {
      auto counters = client_.Get("/api/v1/counters");
      if (!counters) {
        return ui::RenderError(*eng, req, 502, "hd_unreachable",
                               counters.error().message);
      }
      ui::RenderArgs args;
      args.fragment = "hd/counters";
      args.layout = "layout";
      args.data = CountersContext(*counters);
      add_meta(args.meta, "hyper-derp — counters", "counters");
      auto r = ui::Render(*eng, req, args);
      if (!r) {
        return ui::RenderError(*eng, req, 500, "render_failed",
                               r.error().message);
      }
      return std::move(*r);
    });

    CROW_ROUTE(app, "/audit")
    ([eng, this, add_meta](const crow::request &req) {
      auto audit = client_.Get("/api/v1/audit/recent?limit=200");
      if (!audit) {
        return ui::RenderError(*eng, req, 502, "hd_unreachable",
                               audit.error().message);
      }
      ui::RenderArgs args;
      args.fragment = "hd/audit";
      args.layout = "layout";
      args.data = {{"entries", AuditEntries(*audit)}};
      add_meta(args.meta, "hyper-derp — audit", "audit");
      auto r = ui::Render(*eng, req, args);
      if (!r) {
        return ui::RenderError(*eng, req, 500, "render_failed",
                               r.error().message);
      }
      return std::move(*r);
    });
  }

 private:
  HdClient client_;
};

}  // namespace

auto NewHdRelayUiAdapter(HdClientConfig cfg)
    -> std::unique_ptr<ui::ProductUiAdapter> {
  return std::make_unique<HdRelayUiAdapter>(std::move(cfg));
}

}  // namespace einheit::adapters::hd_relay
