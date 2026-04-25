/// @file stream.h
/// @brief WebSocket-based live update channel. Adapters subscribe a
/// topic to a fragment template at startup; later, code that mutates
/// state calls Publish() with a JSON context and the framework
/// renders the fragment, wraps it in HTMX's `hx-swap-oob` envelope,
/// and broadcasts it over the WebSocket connection. HTMX's
/// `htmx-ext-ws` extension performs the out-of-band swap on receipt.
///
/// We picked WebSocket over SSE because Crow's response model
/// buffers the body until `end()` and has no flush primitive, so
/// proper SSE would require either a Crow fork or socket-hijack
/// gymnastics. Crow ships first-class `CROW_WEBSOCKET_ROUTE` with
/// onopen/onclose/onmessage hooks and `connection::send_text(...)`
/// callable from any thread, which fits the framework's
/// "default to push" stance without compromise.
// Copyright (c) 2026 Einheit Networks

#ifndef INCLUDE_EINHEIT_UI_STREAM_H_
#define INCLUDE_EINHEIT_UI_STREAM_H_

#include <expected>
#include <memory>
#include <string>
#include <string_view>

#include <crow.h>
#include <crow/websocket.h>
#include <nlohmann/json.hpp>

#include "einheit/ui/error.h"
#include "einheit/ui/render/template_engine.h"

namespace einheit::ui {

/// Errors raised by the WebSocket stream layer.
enum class StreamError {
  /// Unknown topic — Publish() called before Bind().
  UnknownTopic,
  /// Underlying template render failed.
  TemplateFailed,
};

/// Maps how a published event reaches the DOM. The fragment is
/// rendered with the published context; the result is wrapped in an
/// HTMX out-of-band container that targets `swap_target` so the
/// browser swaps it into the existing element with that id.
struct TopicBinding {
  /// Logical topic name. Subscribers receive every Publish to this.
  std::string topic;
  /// Fragment template to render on each publish.
  std::string fragment;
  /// DOM element id the rendered fragment should swap into.
  std::string swap_target;
  /// HTMX swap strategy: "outerHTML" (default), "innerHTML",
  /// "beforeend", "afterbegin", etc.
  std::string swap_strategy = "outerHTML";
};

/// Wrap a rendered HTML body in HTMX's out-of-band swap container so
/// the receiving browser swaps it into the existing element with id
/// `binding.swap_target` using strategy `binding.swap_strategy`.
/// Public so tests (and adapters that need to format their own
/// payloads) can call it directly.
/// @param body Rendered fragment HTML.
/// @param binding Topic binding describing the swap target.
/// @returns OOB-wrapped HTML.
auto BuildOobWrapper(std::string_view body, const TopicBinding &binding)
    -> std::string;

/// Live WebSocket channel. Constructed once per product; survives
/// the lifetime of the server.
class EventStream {
 public:
  /// @param eng Template engine used to render fragments.
  explicit EventStream(const render::TemplateEngine &eng);
  ~EventStream();

  EventStream(const EventStream &) = delete;
  auto operator=(const EventStream &) -> EventStream & = delete;

  /// Register a topic-to-fragment binding. Must be called before
  /// the corresponding Publish() runs; otherwise Publish returns
  /// UnknownTopic.
  /// @param binding Topic binding to install.
  auto Bind(TopicBinding binding) -> void;

  /// Push an event. Renders `binding.fragment` with `ctx`, wraps it
  /// for HTMX out-of-band swap, and writes it to every connected
  /// WebSocket subscriber.
  /// @param topic Topic name; must have been Bind()ed.
  /// @param ctx JSON context for the fragment.
  /// @returns void on success or StreamError.
  auto Publish(std::string_view topic, const nlohmann::json &ctx)
      -> std::expected<void, Error<StreamError>>;

  /// Mount the WebSocket endpoint at the canonical `/events` path.
  /// Browsers connect via HTMX's `ws-ext` (`hx-ext="ws"
  /// ws-connect="/events"` on the body). The handler tracks the
  /// connection and routes Publish() output to it for the
  /// connection's lifetime.
  /// @param app Crow app.
  auto Mount(crow::SimpleApp &app) -> void;

  /// Connection lifecycle hooks for adapters that want to register
  /// a WebSocket route at a non-default path. Wire them as the
  /// `.onopen()` and `.onclose()` callbacks of a
  /// `CROW_WEBSOCKET_ROUTE` block. `OnMessage` is intentionally
  /// absent — the default channel is server-to-browser only;
  /// adapters that want bidirectional traffic install their own
  /// `.onmessage()`.
  /// @param conn Connection that just connected.
  auto OnOpen(crow::websocket::connection &conn) -> void;

  /// @param conn Connection that just disconnected.
  auto OnClose(crow::websocket::connection &conn) -> void;

  /// Number of currently connected subscribers. Useful in metrics.
  auto SubscriberCount() const -> std::size_t;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace einheit::ui

#endif  // INCLUDE_EINHEIT_UI_STREAM_H_
