/// @file sparkline.cc
// Copyright (c) 2026 Einheit Networks

#include "einheit/ui/sparkline.h"

#include <algorithm>
#include <format>
#include <string>
#include <vector>

namespace einheit::ui {

auto SparklinePoints(const std::vector<double> &values, int width,
                     int height) -> std::string {
  if (values.empty()) return {};
  double mn = values[0], mx = values[0];
  for (const auto v : values) {
    mn = std::min(mn, v);
    mx = std::max(mx, v);
  }
  const double range = (mx - mn) == 0.0 ? 1.0 : (mx - mn);
  std::string out;
  out.reserve(values.size() * 12);
  const auto n = values.size();
  for (std::size_t i = 0; i < n; ++i) {
    const double x =
        n == 1 ? width / 2.0
                : static_cast<double>(i) * width /
                      static_cast<double>(n - 1);
    const double y =
        static_cast<double>(height) -
        (values[i] - mn) / range * static_cast<double>(height);
    if (i > 0) out += ' ';
    out += std::format("{:.1f},{:.1f}", x, y);
  }
  return out;
}

}  // namespace einheit::ui
