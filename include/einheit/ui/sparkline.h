/// @file sparkline.h
/// @brief Tiny inline-SVG sparkline helper. Builds the `points`
/// string for a `<polyline>` so adapters can render compact
/// per-tile trend lines without pulling in uPlot for every cell.
///
/// Output coordinates run x in [0, width], y in [0, height], with
/// the minimum input value mapped to y=height (bottom) and the
/// maximum to y=0 (top). The intended consumer is partials/stat_tile,
/// which embeds the polyline as `<polyline points="...">`.
// Copyright (c) 2026 Einheit Networks

#ifndef INCLUDE_EINHEIT_UI_SPARKLINE_H_
#define INCLUDE_EINHEIT_UI_SPARKLINE_H_

#include <string>
#include <vector>

namespace einheit::ui {

/// Build an SVG polyline points string from a sequence of values.
/// @param values Numeric points; empty -> empty string.
/// @param width Target SVG width in user units (default 60).
/// @param height Target SVG height in user units (default 20).
/// @returns "x0,y0 x1,y1 ..." formatted to one decimal place.
auto SparklinePoints(const std::vector<double> &values,
                     int width = 60, int height = 20) -> std::string;

}  // namespace einheit::ui

#endif  // INCLUDE_EINHEIT_UI_SPARKLINE_H_
