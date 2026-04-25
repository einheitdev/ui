/// @file diff.h
/// @brief Line-level text diff helper used by partials/diff. The
/// implementation is a Hunt-McIlroy / Myers-style LCS — fine for
/// the few-hundred-line config diffs operator UIs render. For a
/// large-file editor diff the CodeMirror MergeView addon is the
/// right hammer; this primitive is for inline diffs in cards
/// (configure → commit, peer rule changes, FWL section diffs).
// Copyright (c) 2026 Einheit Networks

#ifndef INCLUDE_EINHEIT_UI_DIFF_H_
#define INCLUDE_EINHEIT_UI_DIFF_H_

#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace einheit::ui {

/// One line in the rendered diff.
struct DiffLine {
  /// `ctx` (unchanged), `add` (only in b), `del` (only in a).
  std::string kind;
  /// Line number in `a` for ctx/del; 0 for add.
  std::size_t a_lineno = 0;
  /// Line number in `b` for ctx/add; 0 for del.
  std::size_t b_lineno = 0;
  /// Line text without the trailing newline.
  std::string text;
};

/// Compute a line-level unified diff between `a` and `b`. Returns
/// the entire result (no hunk windowing) — keep the inputs small
/// (couple of hundred lines) since the algorithm is O(n*m).
/// @param a First text.
/// @param b Second text.
/// @returns Sequence of DiffLine entries in display order.
auto Diff(std::string_view a, std::string_view b)
    -> std::vector<DiffLine>;

/// Convert a Diff() result into the JSON shape the diff partial
/// expects: `{lines: [{kind, a_lineno, b_lineno, text}, ...]}`.
/// @param diff Result from Diff().
/// @returns JSON object suitable for `args.data["diff"]`.
auto DiffToJson(const std::vector<DiffLine> &diff) -> nlohmann::json;

}  // namespace einheit::ui

#endif  // INCLUDE_EINHEIT_UI_DIFF_H_
