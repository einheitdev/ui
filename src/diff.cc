/// @file diff.cc
// Copyright (c) 2026 Einheit Networks

#include "einheit/ui/diff.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace einheit::ui {
namespace {

auto SplitLines(std::string_view s) -> std::vector<std::string> {
  std::vector<std::string> out;
  std::size_t start = 0;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\n') {
      out.emplace_back(s.substr(start, i - start));
      start = i + 1;
    }
  }
  if (start < s.size()) out.emplace_back(s.substr(start));
  return out;
}

// LCS table for two line vectors. Standard O(n*m) DP.
auto LcsTable(const std::vector<std::string> &a,
              const std::vector<std::string> &b)
    -> std::vector<std::vector<std::size_t>> {
  const auto n = a.size(), m = b.size();
  std::vector<std::vector<std::size_t>> t(
      n + 1, std::vector<std::size_t>(m + 1, 0));
  for (std::size_t i = 1; i <= n; ++i) {
    for (std::size_t j = 1; j <= m; ++j) {
      if (a[i - 1] == b[j - 1]) {
        t[i][j] = t[i - 1][j - 1] + 1;
      } else {
        t[i][j] = std::max(t[i - 1][j], t[i][j - 1]);
      }
    }
  }
  return t;
}

}  // namespace

auto Diff(std::string_view a, std::string_view b)
    -> std::vector<DiffLine> {
  const auto av = SplitLines(a);
  const auto bv = SplitLines(b);
  const auto t = LcsTable(av, bv);

  // Walk the LCS table from the bottom-right corner, emitting
  // del/add/ctx in reverse, then reverse at the end.
  std::vector<DiffLine> rev;
  std::size_t i = av.size(), j = bv.size();
  while (i > 0 && j > 0) {
    if (av[i - 1] == bv[j - 1]) {
      rev.push_back({"ctx", i, j, av[i - 1]});
      --i; --j;
    } else if (t[i - 1][j] >= t[i][j - 1]) {
      rev.push_back({"del", i, 0, av[i - 1]});
      --i;
    } else {
      rev.push_back({"add", 0, j, bv[j - 1]});
      --j;
    }
  }
  while (i > 0) {
    rev.push_back({"del", i, 0, av[i - 1]});
    --i;
  }
  while (j > 0) {
    rev.push_back({"add", 0, j, bv[j - 1]});
    --j;
  }
  std::reverse(rev.begin(), rev.end());
  return rev;
}

auto DiffToJson(const std::vector<DiffLine> &diff)
    -> nlohmann::json {
  nlohmann::json lines = nlohmann::json::array();
  for (const auto &d : diff) {
    lines.push_back({{"kind", d.kind},
                     {"a_lineno", d.a_lineno},
                     {"b_lineno", d.b_lineno},
                     {"text", d.text}});
  }
  return {{"lines", std::move(lines)}};
}

}  // namespace einheit::ui
