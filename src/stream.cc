/// @file stream.cc
/// @brief WebSocket-backed EventStream. Connections are tracked in
/// a guarded set; Publish renders the bound fragment, wraps it for
/// HTMX out-of-band swap, and fans out via `connection::send_text`.
// Copyright (c) 2026 Einheit Networks

#include "einheit/ui/stream.h"

#include <deque>
#include <format>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

namespace einheit::ui {
namespace {

auto MakeError(StreamError code, std::string msg)
    -> Error<StreamError> {
  return Error<StreamError>{code, std::move(msg)};
}

}  // namespace

auto BuildOobWrapper(std::string_view body, const TopicBinding &b)
    -> std::string {
  return std::format(
      R"(<div hx-swap-oob="{}:#{}">{}</div>)",
      b.swap_strategy, b.swap_target, body);
}

struct EventStream::Impl {
  const render::TemplateEngine *eng;
  std::unordered_map<std::string, TopicBinding> bindings;
  mutable std::mutex mu;
  std::unordered_set<crow::websocket::connection *> connections;
  // Metric stream — separate connection set + per-topic ring
  // buffer. Lives behind the same mutex as `connections`/
  // `bindings` because publish hot-paths only need one lock.
  std::unordered_set<crow::websocket::connection *> metric_conns;
  std::unordered_map<std::string, std::deque<nlohmann::json>> ring;
};

EventStream::EventStream(const render::TemplateEngine &eng)
    : impl_(std::make_unique<Impl>()) {
  impl_->eng = &eng;
}

EventStream::~EventStream() = default;

auto EventStream::Bind(TopicBinding binding) -> void {
  std::lock_guard<std::mutex> lock(impl_->mu);
  const auto topic = binding.topic;
  impl_->bindings.insert_or_assign(topic, std::move(binding));
}

auto EventStream::Publish(std::string_view topic,
                          const nlohmann::json &ctx)
    -> std::expected<void, Error<StreamError>> {
  TopicBinding binding;
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    auto it = impl_->bindings.find(std::string{topic});
    if (it == impl_->bindings.end()) {
      return std::unexpected(MakeError(
          StreamError::UnknownTopic,
          std::format("no binding for topic '{}'", topic)));
    }
    binding = it->second;
  }

  auto rendered = impl_->eng->Render(binding.fragment, ctx);
  if (!rendered) {
    return std::unexpected(MakeError(
        StreamError::TemplateFailed, rendered.error().message));
  }
  const auto frame = BuildOobWrapper(*rendered, binding);

  // Snapshot the subscriber set so we don't hold the lock across
  // socket I/O. send_text on a closing connection is safe per
  // Crow's contract — we'll see the eventual onclose and remove it.
  std::vector<crow::websocket::connection *> snap;
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    snap.reserve(impl_->connections.size());
    for (auto *c : impl_->connections) snap.push_back(c);
  }
  for (auto *c : snap) c->send_text(frame);
  return {};
}

auto EventStream::OnOpen(crow::websocket::connection &conn) -> void {
  std::lock_guard<std::mutex> lock(impl_->mu);
  impl_->connections.insert(&conn);
  spdlog::debug("ws open from {} ({} total)",
                conn.get_remote_ip(), impl_->connections.size());
}

auto EventStream::OnClose(crow::websocket::connection &conn) -> void {
  std::lock_guard<std::mutex> lock(impl_->mu);
  impl_->connections.erase(&conn);
  spdlog::debug("ws close ({} remaining)",
                impl_->connections.size());
}

auto EventStream::Mount(crow::SimpleApp &app) -> void {
  CROW_WEBSOCKET_ROUTE(app, "/events")
      .onopen([this](crow::websocket::connection &conn) {
        OnOpen(conn);
      })
      .onclose([this](crow::websocket::connection &conn,
                      const std::string & /*reason*/,
                      uint16_t /*code*/) { OnClose(conn); })
      .onmessage([](crow::websocket::connection & /*conn*/,
                    const std::string & /*data*/,
                    bool /*is_binary*/) {
        // Server-to-browser only by default. Adapters that want to
        // accept inbound messages register their own ws route with
        // a custom onmessage and forward open/close to OnOpen/OnClose.
      });
}

auto EventStream::SubscriberCount() const -> std::size_t {
  std::lock_guard<std::mutex> lock(impl_->mu);
  return impl_->connections.size();
}

auto EventStream::PublishToast(std::string_view severity,
                               std::string_view text) -> void {
  // Severity maps to a Lucide icon from the vendored sprite.
  std::string sev{severity};
  if (sev != "good" && sev != "warn" && sev != "bad" &&
      sev != "info") {
    sev = "info";
  }
  std::string icon = "info";
  if (sev == "good") icon = "circle-check";
  else if (sev == "warn") icon = "triangle-alert";
  else if (sev == "bad") icon = "circle-x";

  nlohmann::json ctx{
      {"severity", sev},
      {"icon", icon},
      {"text", std::string{text}},
  };
  auto rendered = impl_->eng->Render("partials/toast", ctx);
  if (!rendered) {
    spdlog::warn("toast render failed: {}",
                 rendered.error().message);
    return;
  }
  // OOB swap into the layout's #toasts stack. afterbegin so the
  // newest toast lands at the top.
  const auto frame = std::format(
      R"(<div hx-swap-oob="afterbegin:#toasts">{}</div>)",
      *rendered);
  std::vector<crow::websocket::connection *> snap;
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    snap.reserve(impl_->connections.size());
    for (auto *c : impl_->connections) snap.push_back(c);
  }
  for (auto *c : snap) c->send_text(frame);
}

auto EventStream::PublishData(std::string_view topic,
                              const nlohmann::json &point) -> void {
  // Append to ring buffer, prune to capacity, snapshot the
  // metric subscriber set, broadcast outside the lock.
  nlohmann::json frame;
  frame["type"] = "data";
  frame["topic"] = std::string{topic};
  frame["point"] = point;
  const auto serialized = frame.dump();

  std::vector<crow::websocket::connection *> snap;
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    auto &q = impl_->ring[std::string{topic}];
    q.push_back(point);
    while (q.size() > kRingCapacity) q.pop_front();
    snap.reserve(impl_->metric_conns.size());
    for (auto *c : impl_->metric_conns) snap.push_back(c);
  }
  for (auto *c : snap) c->send_text(serialized);
}

auto EventStream::RecentPoints(std::string_view topic) const
    -> nlohmann::json {
  nlohmann::json out = nlohmann::json::array();
  std::lock_guard<std::mutex> lock(impl_->mu);
  auto it = impl_->ring.find(std::string{topic});
  if (it == impl_->ring.end()) return out;
  for (const auto &p : it->second) out.push_back(p);
  return out;
}

auto EventStream::MountMetrics(crow::SimpleApp &app) -> void {
  CROW_WEBSOCKET_ROUTE(app, "/metrics/ws")
      .onopen([this](crow::websocket::connection &conn) {
        std::lock_guard<std::mutex> lock(impl_->mu);
        impl_->metric_conns.insert(&conn);
      })
      .onclose([this](crow::websocket::connection &conn,
                      const std::string &, uint16_t) {
        std::lock_guard<std::mutex> lock(impl_->mu);
        impl_->metric_conns.erase(&conn);
      })
      .onmessage([](crow::websocket::connection &,
                    const std::string &, bool) {
        // Server-to-browser only.
      });
}

}  // namespace einheit::ui
