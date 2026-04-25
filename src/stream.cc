/// @file stream.cc
/// @brief SSE event-stream implementation. The connection-management
/// loop is intentionally a sketch — Crow's streaming-response API
/// has version drift, so the broadcast hook is wired through a
/// std::function that the Mount() variant in active use binds to a
/// concrete Crow streaming primitive.
// Copyright (c) 2026 Einheit Networks

#include "einheit/ui/stream.h"

#include <chrono>
#include <format>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

namespace einheit::ui {
namespace {

auto MakeError(StreamError code, std::string msg)
    -> Error<StreamError> {
  return Error<StreamError>{code, std::move(msg)};
}

/// One connected SSE client. Holds a writer callback the framework
/// invokes per published event.
struct Subscriber {
  std::uint64_t id = 0;
  std::function<void(const std::string &)> write_chunk;
};

}  // namespace

auto BuildOobWrapper(std::string_view body, const TopicBinding &b)
    -> std::string {
  return std::format(
      R"(<div hx-swap-oob="{}:#{}">{}</div>)"
      "\n",
      b.swap_strategy, b.swap_target, body);
}

auto SseFrame(std::string_view payload) -> std::string {
  std::string out;
  out.reserve(payload.size() + 16);
  std::size_t start = 0;
  while (start < payload.size()) {
    auto nl = payload.find('\n', start);
    auto line = payload.substr(
        start, nl == std::string_view::npos ? std::string_view::npos
                                            : nl - start);
    out += "data: ";
    out += line;
    out += '\n';
    if (nl == std::string_view::npos) break;
    start = nl + 1;
  }
  out += '\n';
  return out;
}

struct EventStream::Impl {
  const render::TemplateEngine *eng;
  std::unordered_map<std::string, TopicBinding> bindings;
  mutable std::mutex mu;
  std::vector<Subscriber> subscribers;
  std::uint64_t next_id = 1;
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
  const auto frame = SseFrame(BuildOobWrapper(*rendered, binding));

  std::vector<Subscriber> snap;
  {
    std::lock_guard<std::mutex> lock(impl_->mu);
    snap = impl_->subscribers;
  }
  for (auto &s : snap) {
    if (s.write_chunk) s.write_chunk(frame);
  }
  return {};
}

auto EventStream::Mount(crow::SimpleApp &app, std::string_view path)
    -> void {
  // TODO(stream): replace with the streaming-response form actually
  // exposed by the Crow version we pin in fetch.cmake. The shape:
  //   - set Content-Type: text/event-stream
  //   - register a Subscriber with a write_chunk callback
  //   - on disconnect, deregister the subscriber
  //   - emit a heartbeat comment ":heartbeat\n\n" every 15s
  // This stub keeps the build green while the API choice is
  // finalised.
  const std::string p{path};
  CROW_ROUTE(app, "/_stream_unbound")
  ([p]() {
    spdlog::warn(
        "EventStream::Mount stub hit; SSE endpoint '{}' not "
        "wired yet",
        p);
    crow::response r{503, "SSE endpoint not implemented in this build"};
    r.set_header("Content-Type", "text/plain; charset=utf-8");
    return r;
  });
  spdlog::info("EventStream mount placeholder for path '{}'", p);
}

auto EventStream::SubscriberCount() const -> std::size_t {
  std::lock_guard<std::mutex> lock(impl_->mu);
  return impl_->subscribers.size();
}

}  // namespace einheit::ui
