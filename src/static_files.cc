/// @file static_files.cc
// Copyright (c) 2026 Einheit Networks

#include "einheit/ui/static_files.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>

#include <spdlog/spdlog.h>

namespace einheit::ui {
namespace {

const std::unordered_map<std::string, std::string> &MimeTable() {
  static const std::unordered_map<std::string, std::string> kT{
      {".js", "application/javascript; charset=utf-8"},
      {".css", "text/css; charset=utf-8"},
      {".html", "text/html; charset=utf-8"},
      {".svg", "image/svg+xml"},
      {".png", "image/png"},
      {".ico", "image/x-icon"},
      {".woff2", "font/woff2"},
      {".json", "application/json; charset=utf-8"},
      {".map", "application/json; charset=utf-8"},
  };
  return kT;
}

auto IsSafeRel(std::string_view rel) -> bool {
  if (rel.empty()) return false;
  if (rel.find("..") != std::string_view::npos) return false;
  if (rel.front() == '/') return false;
  return true;
}

auto ReadAll(const std::filesystem::path &p) -> std::string {
  std::ifstream f(p, std::ios::binary);
  if (!f) return {};
  std::ostringstream out;
  out << f.rdbuf();
  return out.str();
}

}  // namespace

auto MountStatic(crow::SimpleApp &app, std::string_view mount_at,
                 std::string_view dir) -> void {
  const std::string root_str{dir};
  const std::filesystem::path root =
      std::filesystem::weakly_canonical(root_str);
  std::string prefix{mount_at};
  if (!prefix.empty() && prefix.back() == '/') prefix.pop_back();
  const std::string route_pattern = prefix + "/<path>";

  app.route_dynamic(route_pattern)
      ([root](std::string rel) {
        if (!IsSafeRel(rel)) {
          return crow::response{400, "bad path"};
        }
        std::error_code ec;
        auto candidate =
            std::filesystem::weakly_canonical(root / rel, ec);
        if (ec) return crow::response{404};
        const auto root_str_ = root.string();
        if (candidate.string().rfind(root_str_, 0) != 0) {
          return crow::response{403, "forbidden"};
        }
        if (!std::filesystem::is_regular_file(candidate, ec) || ec) {
          return crow::response{404};
        }
        crow::response r{200, ReadAll(candidate)};
        const auto ext = candidate.extension().string();
        if (auto it = MimeTable().find(ext); it != MimeTable().end()) {
          r.set_header("Content-Type", it->second);
        } else {
          r.set_header("Content-Type", "application/octet-stream");
        }
        r.set_header("Cache-Control",
                     "public, max-age=300, must-revalidate");
        return r;
      });
  spdlog::info("mounted static dir '{}' at '{}'", root.string(),
               prefix);
}

}  // namespace einheit::ui
