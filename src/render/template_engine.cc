/// @file template_engine.cc
// Copyright (c) 2026 Einheit Networks

#include "einheit/ui/render/template_engine.h"

#include <filesystem>
#include <format>
#include <stdexcept>
#include <string>
#include <utility>

#include <inja/inja.hpp>
#include <spdlog/spdlog.h>

namespace einheit::ui::render {

struct TemplateEngine::Impl {
  TemplateEngineConfig cfg;
  inja::Environment env;
};

namespace {

auto MakeError(TemplateError code, std::string msg)
    -> Error<TemplateError> {
  return Error<TemplateError>{code, std::move(msg)};
}

}  // namespace

TemplateEngine::TemplateEngine(TemplateEngineConfig cfg)
    : impl_(std::make_unique<Impl>(Impl{std::move(cfg), {}})) {
  // inja's default `{% include %}` resolves relative to the parent
  // template's directory. That breaks cross-root composition (e.g.
  // an adapter template including a framework partial), so we
  // override the loader to walk our multi-root search path
  // instead.
  impl_->env.set_search_included_templates_in_files(true);
  impl_->env.set_html_autoescape(impl_->cfg.auto_escape);

  impl_->env.set_include_callback(
      [this](const std::filesystem::path & /*base*/,
             const std::string &target) -> inja::Template {
        auto resolved = Resolve(target);
        if (!resolved) {
          throw inja::FileError(resolved.error().message);
        }
        return impl_->env.parse_template(*resolved);
      });
}

TemplateEngine::~TemplateEngine() = default;

TemplateEngine::TemplateEngine(TemplateEngine &&) noexcept = default;
auto TemplateEngine::operator=(TemplateEngine &&) noexcept
    -> TemplateEngine & = default;

auto TemplateEngine::Resolve(std::string_view name) const
    -> std::expected<std::string, Error<TemplateError>> {
  const std::string n{name};
  // Candidate filenames to try, in order. The default-suffix path
  // (e.g. "partials/card" -> "partials/card.html.inja") wins on
  // first hit; the bare ".inja" alternate handles non-HTML
  // templates like "theme.css.inja"; the literal name handles
  // callers that already pass a full filename.
  std::vector<std::string> candidates;
  if (!n.ends_with(".inja")) {
    candidates.push_back(n + impl_->cfg.default_suffix);
    candidates.push_back(n + ".inja");
  }
  candidates.push_back(n);
  for (const auto &root : impl_->cfg.search_paths) {
    for (const auto &cand : candidates) {
      auto path = std::filesystem::path(root) / cand;
      std::error_code ec;
      if (std::filesystem::exists(path, ec) && !ec) {
        return path.string();
      }
    }
  }
  return std::unexpected(MakeError(
      TemplateError::NotFound,
      std::format("template '{}' not found in any search path", n)));
}

auto TemplateEngine::Render(std::string_view name,
                            const nlohmann::json &ctx) const
    -> std::expected<std::string, Error<TemplateError>> {
  auto path = Resolve(name);
  if (!path) return std::unexpected(path.error());
  try {
    if (impl_->cfg.hot_reload) {
      // parse_template re-reads from disk every call; cheap enough
      // for dev mode and avoids stale-template debugging.
      auto tpl = impl_->env.parse_template(*path);
      return impl_->env.render(tpl, ctx);
    }
    return impl_->env.render_file(*path, ctx);
  } catch (const inja::ParserError &e) {
    return std::unexpected(MakeError(
        TemplateError::ParseFailed,
        std::format("parse '{}': {}", *path, e.what())));
  } catch (const inja::RenderError &e) {
    return std::unexpected(MakeError(
        TemplateError::RenderFailed,
        std::format("render '{}': {}", *path, e.what())));
  } catch (const inja::FileError &e) {
    return std::unexpected(MakeError(
        TemplateError::IoFailed,
        std::format("read '{}': {}", *path, e.what())));
  } catch (const std::exception &e) {
    return std::unexpected(MakeError(
        TemplateError::RenderFailed,
        std::format("render '{}': {}", *path, e.what())));
  }
}

auto TemplateEngine::RenderString(std::string_view source,
                                  const nlohmann::json &ctx) const
    -> std::expected<std::string, Error<TemplateError>> {
  try {
    return impl_->env.render(std::string{source}, ctx);
  } catch (const inja::ParserError &e) {
    return std::unexpected(MakeError(
        TemplateError::ParseFailed,
        std::format("parse inline: {}", e.what())));
  } catch (const inja::RenderError &e) {
    return std::unexpected(MakeError(
        TemplateError::RenderFailed,
        std::format("render inline: {}", e.what())));
  } catch (const std::exception &e) {
    return std::unexpected(MakeError(
        TemplateError::RenderFailed,
        std::format("render inline: {}", e.what())));
  }
}

}  // namespace einheit::ui::render
