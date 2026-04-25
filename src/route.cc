/// @file route.cc
// Copyright (c) 2026 Einheit Networks

#include "einheit/ui/route.h"

#include <atomic>
#include <format>
#include <mutex>
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

namespace einheit::ui {
namespace {

auto MakeError(RouteError code, std::string msg)
    -> Error<RouteError> {
  return Error<RouteError>{code, std::move(msg)};
}

// Process-global "where does /shell live, if anywhere" string. Set
// once at startup by the UI binary when --shell is on; read on
// every Page-format Render to inject meta.shell into the layout.
// A mutex rather than an atomic<string> because std::string isn't
// trivially copyable.
std::mutex &ShellMu() {
  static std::mutex m;
  return m;
}
std::string &ShellPath() {
  static std::string p;
  return p;
}

// Process-global fallbacks for meta.nav and meta.brand. Each
// adapter route used to spell these out by hand; with the
// sidebar layout there is exactly one canonical value for both
// per process, so the UI binary sets them once at startup and
// Render() backfills any route that forgot. Same mutex/value
// pattern as ShellPath above.
std::mutex &PrimaryMu() {
  static std::mutex m;
  return m;
}
nlohmann::json &PrimaryNav() {
  static nlohmann::json n = nlohmann::json::array();
  return n;
}
std::string &PrimaryBrand() {
  static std::string b;
  return b;
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

auto SetLayoutShellPath(std::string path) -> void {
  std::lock_guard<std::mutex> lk(ShellMu());
  ShellPath() = std::move(path);
}

auto LayoutShellPath() -> std::string {
  std::lock_guard<std::mutex> lk(ShellMu());
  return ShellPath();
}

auto SetLayoutPrimaryNav(nlohmann::json nav) -> void {
  std::lock_guard<std::mutex> lk(PrimaryMu());
  PrimaryNav() = std::move(nav);
}

auto SetLayoutPrimaryBrand(std::string brand) -> void {
  std::lock_guard<std::mutex> lk(PrimaryMu());
  PrimaryBrand() = std::move(brand);
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
      // Backfill the meta keys the shared layout reads. inja raises
      // on missing variable access (no Jinja-style `is defined`),
      // so the framework guarantees these exist even when the
      // adapter didn't bother. Adapter-supplied values win; only
      // absent keys get a default.
      nlohmann::json meta = args.meta.is_object()
                                ? args.meta
                                : nlohmann::json::object();
      if (!meta.contains("title")) meta["title"] = "einheit";
      if (!meta.contains("brand")) {
        std::string brand;
        {
          std::lock_guard<std::mutex> lk(PrimaryMu());
          brand = PrimaryBrand();
        }
        meta["brand"] = brand.empty() ? std::string("einheit") : brand;
      }
      if (!meta.contains("active")) meta["active"] = "";
      if (!meta.contains("nav")) {
        std::lock_guard<std::mutex> lk(PrimaryMu());
        meta["nav"] = PrimaryNav();
      }
      // Inject the framework's optional /shell entry. Adapters
      // never need to know about the shell module — the UI binary
      // sets the path once on startup, and every Page render
      // automatically gets it.
      if (!meta.contains("shell")) {
        std::string path;
        {
          std::lock_guard<std::mutex> lk(ShellMu());
          path = ShellPath();
        }
        meta["shell"] = path;
      }
      nlohmann::json layout_ctx = nlohmann::json::object();
      layout_ctx["meta"] = std::move(meta);
      layout_ctx["data"] = args.data;
      auto page = eng.Render(args.layout, layout_ctx);
      if (!page) {
        return std::unexpected(MakeError(
            RouteError::TemplateFailed, page.error().message));
      }
      // The layout template carries a literal placeholder that we
      // splice the rendered fragment into. Doing it post-render
      // avoids fighting inja's autoescape over a value that is
      // already trusted HTML — meta.* etc. still get escaped
      // normally inside the layout render.
      static constexpr std::string_view kBodyMarker =
          "<!--EINHEIT_BODY-->";
      if (auto pos = page->find(kBodyMarker);
          pos != std::string::npos) {
        page->replace(pos, kBodyMarker.size(), *inner);
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
