/// @file stream.h
/// @brief Server-Sent Events bridge. Adapters subscribe a topic to a
/// fragment template at startup; later, code that mutates state
/// calls Publish() with a JSON context and the framework renders the
/// fragment and pushes it to every connected browser as an SSE
/// `message` whose body is `<div hx-swap-oob=...>...</div>` so HTMX
/// performs an out-of-band swap into the named target.
///
/// The push path is the default for live updates; HTMX polling stays
/// available as an escape hatch for surfaces where push is overkill.
// Copyright (c) 2026 Einheit Networks

#ifndef INCLUDE_EINHEIT_UI_STREAM_H_
#define INCLUDE_EINHEIT_UI_STREAM_H_

#include <expected>
#include <memory>
#include <string>
#include <string_view>

#include <crow.h>
#include <nlohmann/json.hpp>

#include "einheit/ui/error.h"
#include "einheit/ui/render/template_engine.h"

namespace einheit::ui {

/// Errors raised by the SSE stream layer.
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

/// Encode a payload as a Server-Sent Events `data:` frame. Multi-line
/// payloads emit one `data:` line per line; the frame ends with a
/// blank line per the SSE spec.
/// @param payload Raw text to encode (typically OOB-wrapped HTML).
/// @returns SSE wire-format frame.
auto SseFrame(std::string_view payload) -> std::string;

/// Live SSE channel mounted on a Crow app. Constructed once per
/// product; survives the lifetime of the server.
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
  /// SSE subscriber.
  /// @param topic Topic name; must have been Bind()ed.
  /// @param ctx JSON context for the fragment.
  /// @returns void on success or StreamError.
  auto Publish(std::string_view topic, const nlohmann::json &ctx)
      -> std::expected<void, Error<StreamError>>;

  /// Mount the SSE endpoint onto a Crow app at `path`. Browsers open
  /// `new EventSource(path)` here. The handler holds the connection
  /// open; the framework writes a heartbeat comment every 15s to
  /// keep proxies from timing the connection out.
  /// @param app Crow app.
  /// @param path Public URL path (e.g. "/events").
  auto Mount(crow::SimpleApp &app, std::string_view path) -> void;

  /// Number of currently connected subscribers. Useful in metrics.
  auto SubscriberCount() const -> std::size_t;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace einheit::ui

#endif  // INCLUDE_EINHEIT_UI_STREAM_H_
