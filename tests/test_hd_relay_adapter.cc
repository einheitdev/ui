/// @file test_hd_relay_adapter.cc
/// @brief Adapter-shape tests for hd-relay. Doesn't stand up a
/// real Hyper-DERP daemon — the live HTTP path belongs in an
/// integration tier. This file checks the slug/nav/template
/// contract and renders the adapter's templates against fake
/// JSON shaped like hd's `/api/v1/...` responses to make sure
/// they don't crash on the data the daemon actually emits.
// Copyright (c) 2026 Einheit Networks

#include <regex>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "einheit/adapters/hd_relay/hd_client.h"
#include "einheit/adapters/hd_relay/ui_adapter.h"
#include "einheit/ui/adapter.h"
#include "einheit/ui/render/template_engine.h"

namespace einheit::adapters::hd_relay {

TEST(HdRelayAdapter, FactoryReturnsAdapter) {
  HdClientConfig cfg;
  cfg.base_url = "http://127.0.0.1:1";
  auto a = NewHdRelayUiAdapter(std::move(cfg));
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->Slug(), "hd-relay");
}

TEST(HdRelayAdapter, SlugMatchesContract) {
  HdClientConfig cfg;
  auto a = NewHdRelayUiAdapter(std::move(cfg));
  static const std::regex kSlug(R"(^[a-z][a-z0-9-]*$)");
  EXPECT_TRUE(std::regex_match(a->Slug(), kSlug)) << a->Slug();
}

TEST(HdRelayAdapter, NavCoversTopLevelRoutes) {
  HdClientConfig cfg;
  auto a = NewHdRelayUiAdapter(std::move(cfg));
  std::vector<std::string> slugs;
  for (const auto &n : a->Nav()) slugs.push_back(n.slug);
  for (const auto *expected :
       {"overview", "peers", "counters", "audit"}) {
    EXPECT_NE(std::find(slugs.begin(), slugs.end(), expected),
              slugs.end())
        << "nav missing slug: " << expected;
  }
}

TEST(HdRelayAdapter, TemplatesResolveAndRenderWithFakeJson) {
  HdClientConfig cfg;
  auto a = NewHdRelayUiAdapter(std::move(cfg));
  ui::render::TemplateEngineConfig tcfg;
  tcfg.search_paths = {a->TemplatesDir(),
                       EINHEIT_UI_FRAMEWORK_TEMPLATES_DIR};
  ui::render::TemplateEngine eng(std::move(tcfg));

  // Overview — fed status rows like the adapter would build them
  // from /api/v1/relay.
  nlohmann::json overview = {
      {"rows",
       nlohmann::json::array(
           {{{"label", "hd"},
             {"value", "enabled"},
             {"semantic", "good"}},
            {{"label", "peers"}, {"value", "3"}},
            {{"label", "workers"}, {"value", "8"}}})}};
  auto ov = eng.Render("hd/overview", overview);
  ASSERT_TRUE(ov.has_value()) << ov.error().message;
  EXPECT_NE(ov->find("relay status"), std::string::npos);
  EXPECT_NE(ov->find("enabled"), std::string::npos);
  EXPECT_NE(ov->find("→ peers"), std::string::npos);

  // Peers table, including the pending state that surfaces both
  // approve and deny buttons via hx-post URLs.
  nlohmann::json peers = {
      {"peers",
       nlohmann::json::array(
           {{{"key", "deadbeef"},
             {"key_str", "ck_deadbeef"},
             {"state", "pending"},
             {"state_semantic", "warn"},
             {"rule_count", 0},
             {"fd", 12}},
            {{"key", "cafef00d"},
             {"key_str", "ck_cafef00d"},
             {"state", "approved"},
             {"state_semantic", "good"},
             {"rule_count", 2},
             {"fd", 7}}})}};
  auto pe = eng.Render("hd/peers", peers);
  ASSERT_TRUE(pe.has_value()) << pe.error().message;
  EXPECT_NE(pe->find("ck_deadbeef"), std::string::npos);
  EXPECT_NE(pe->find("ck_cafef00d"), std::string::npos);
  EXPECT_NE(pe->find(R"(hx-post="/peers/deadbeef/approve")"),
            std::string::npos);
  EXPECT_NE(pe->find(R"(hx-post="/peers/deadbeef/deny")"),
            std::string::npos);
  // Approved peer should expose revoke (= deny), not approve.
  EXPECT_NE(pe->find(R"(hx-post="/peers/cafef00d/deny")"),
            std::string::npos);
  EXPECT_EQ(pe->find(R"(hx-post="/peers/cafef00d/approve")"),
            std::string::npos);

  // peer_row standalone — used as the POST-action fragment.
  // Adapter wraps the row as {p: ...}; verify that path renders.
  nlohmann::json wrapped = {
      {"p", {{"key", "deadbeef"},
             {"key_str", "ck_deadbeef"},
             {"state", "approved"},
             {"state_semantic", "good"},
             {"rule_count", 0},
             {"fd", 12}}}};
  auto pr = eng.Render("hd/peer_row", wrapped);
  ASSERT_TRUE(pr.has_value()) << pr.error().message;
  EXPECT_NE(pr->find(R"(id="peer-deadbeef")"), std::string::npos);
  EXPECT_NE(pr->find("revoke"), std::string::npos);

  // Empty peers: should fall through to the empty placeholder.
  auto pe_empty = eng.Render(
      "hd/peers", {{"peers", nlohmann::json::array()}});
  ASSERT_TRUE(pe_empty.has_value()) << pe_empty.error().message;
  EXPECT_NE(pe_empty->find("no peers enrolled yet"),
            std::string::npos);

  // Counters: totals + per-worker rows + plot configs. The
  // counters template now embeds two realtime plots driven by
  // the adapter's background sampler, so the test data has to
  // include their plot configs even though no data flows in
  // this offline render.
  auto plot = [](const std::string &topic,
                 const std::string &title,
                 const std::string &y_label,
                 nlohmann::json series) {
    return nlohmann::json{
        {"topic", topic},     {"title", title},
        {"y_label", y_label}, {"window_s", 300},
        {"series", std::move(series)},
        {"points", nlohmann::json::array()},
        {"height", 180}};
  };
  nlohmann::json counters = {
      {"totals",
       nlohmann::json::array(
           {{{"label", "total RX"}, {"value", "12.0 MiB"}},
            {{"label", "send drops"},
             {"value", "0"},
             {"semantic", "good"}}})},
      {"workers",
       nlohmann::json::array(
           {nlohmann::json::array(
               {{{"text", "0"}},
                {{"text", "10 KiB"}, {"align", "right"}},
                {{"text", "20 KiB"}, {"align", "right"}},
                {{"text", "0"},
                 {"align", "right"},
                 {"semantic", "default"}}})})},
      {"throughput_plot",
       plot("hd.io_bps", "throughput", "B/s",
            nlohmann::json::array(
                {{{"label", "rx"}, {"semantic", "good"}},
                 {{"label", "tx"}, {"semantic", "info"}}}))},
      {"forwards_plot",
       plot("hd.fwd_per_sec", "forwards/sec", "fwd/s",
            nlohmann::json::array(
                {{{"label", "mesh"}, {"semantic", "accent"}},
                 {{"label", "fleet"}, {"semantic", "warn"}}}))}};
  auto co = eng.Render("hd/counters", counters);
  ASSERT_TRUE(co.has_value()) << co.error().message;
  EXPECT_NE(co->find("total RX"), std::string::npos);
  EXPECT_NE(co->find("12.0 MiB"), std::string::npos);
  EXPECT_NE(co->find("10 KiB"), std::string::npos);

  // Audit — uses the framework log_entry partial.
  nlohmann::json audit = {
      {"entries",
       nlohmann::json::array(
           {{{"timestamp", "2026-04-25T10:00:00Z"},
             {"level", "INFO"},
             {"message", "approved ck_aaaa → ck_bbbb"}}})}};
  auto au = eng.Render("hd/audit", audit);
  ASSERT_TRUE(au.has_value()) << au.error().message;
  EXPECT_NE(au->find("approved ck_aaaa"), std::string::npos);
}

TEST(HdClient, BadUrlSurfacesAtFirstRequest) {
  HdClientConfig cfg;
  cfg.base_url = "no-scheme-host";
  HdClient c(std::move(cfg));
  auto r = c.Get("/api/v1/relay");
  ASSERT_FALSE(r.has_value());
  EXPECT_EQ(r.error().code, HdClientError::BadUrl);
}

}  // namespace einheit::adapters::hd_relay
