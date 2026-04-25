/// @file stream.cc
/// @brief WebSocket-backed EventStream. Connections are tracked in
/// a guarded set; Publish renders the bound fragment, wraps it for
/// HTMX out-of-band swap, and fans out via `connection::send_text`.
// Copyright (c) 2026 Einheit Networks

#include "einheit/ui/stream.h"

#include <format>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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

}  // namespace einheit::ui
