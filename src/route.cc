/// @file route.cc
// Copyright (c) 2026 Einheit Networks

#include "einheit/ui/route.h"

#include <format>
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

namespace einheit::ui {
namespace {

auto MakeError(RouteError code, std::string msg)
    -> Error<RouteError> {
  return Error<RouteError>{code, std::move(msg)};
}

auto Lower(std::string s) -> std::string {
  for (auto &ch : s) ch = static_cast<char>(std::tolower(ch));
  return s;
}

auto QueryParam(const crow::request &req, const std::string &key)
    -> std::string {
  const auto *v = req.url_params.get(key.c_str());
  return v ? std::string{v} : std::string{};
}

}  // namespace

auto IsHxRequest(const crow::request &req) -> bool {
  return Lower(req.get_header_value("HX-Request")) == "true";
}

auto DetectFormat(const crow::request &req) -> ResponseFormat {
  const auto explicit_fmt = Lower(QueryParam(req, "format"));
  if (explicit_fmt == "json") return ResponseFormat::Json;
  if (explicit_fmt == "html") return ResponseFormat::Page;
  if (explicit_fmt == "fragment") return ResponseFormat::Fragment;
  if (IsHxRequest(req)) return ResponseFormat::Fragment;
  const auto accept = Lower(req.get_header_value("Accept"));
  if (accept.find("application/json") != std::string::npos) {
    return ResponseFormat::Json;
  }
  return ResponseFormat::Page;
}

auto Render(const render::TemplateEngine &eng, ResponseFormat fmt,
            const RenderArgs &args)
    -> std::expected<crow::response, Error<RouteError>> {
  switch (fmt) {
    case ResponseFormat::Json: {
      crow::response r{200, args.data.dump()};
      r.set_header("Content-Type", "application/json; charset=utf-8");
      return r;
    }
    case ResponseFormat::Fragment: {
      auto body = eng.Render(args.fragment, args.data);
      if (!body) {
        return std::unexpected(MakeError(
            RouteError::TemplateFailed, body.error().message));
      }
      crow::response r{200, *body};
      r.set_header("Content-Type", "text/html; charset=utf-8");
      r.set_header("Vary", "HX-Request, Accept");
      return r;
    }
    case ResponseFormat::Page: {
      auto inner = eng.Render(args.fragment, args.data);
      if (!inner) {
        return std::unexpected(MakeError(
            RouteError::TemplateFailed, inner.error().message));
      }
      nlohmann::json layout_ctx = nlohmann::json::object();
      layout_ctx["body"] = *inner;
      layout_ctx["meta"] = args.meta;
      layout_ctx["data"] = args.data;
      auto page = eng.Render(args.layout, layout_ctx);
      if (!page) {
        return std::unexpected(MakeError(
            RouteError::TemplateFailed, page.error().message));
      }
      crow::response r{200, *page};
      r.set_header("Content-Type", "text/html; charset=utf-8");
      r.set_header("Vary", "HX-Request, Accept");
      return r;
    }
  }
  return std::unexpected(MakeError(
      RouteError::UnsupportedFormat, "unknown ResponseFormat"));
}

auto Render(const render::TemplateEngine &eng,
            const crow::request &req, const RenderArgs &args)
    -> std::expected<crow::response, Error<RouteError>> {
  return Render(eng, DetectFormat(req), args);
}

auto RenderError(const render::TemplateEngine &eng,
                 const crow::request &req, int status,
                 std::string_view code, std::string_view message,
                 std::string_view hint) -> crow::response {
  const auto fmt = DetectFormat(req);
  if (fmt == ResponseFormat::Json) {
    nlohmann::json body{
        {"code", code}, {"message", message}, {"hint", hint}};
    crow::response r{status, body.dump()};
    r.set_header("Content-Type", "application/json; charset=utf-8");
    return r;
  }
  RenderArgs args;
  args.fragment = "partials/error";
  args.layout = "layout";
  args.data = {{"code", code}, {"message", message}, {"hint", hint}};
  args.meta = {{"title", "error"}};
  if (auto rendered = Render(eng, fmt, args); rendered) {
    rendered->code = status;
    return std::move(*rendered);
  } else {
    spdlog::warn("error template missing, falling back to plaintext: {}",
                 rendered.error().message);
    crow::response r{status,
                     std::format("{}: {}\n{}", code, message, hint)};
    r.set_header("Content-Type", "text/plain; charset=utf-8");
    return r;
  }
}

}  // namespace einheit::ui
