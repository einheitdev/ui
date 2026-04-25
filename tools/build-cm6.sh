#!/usr/bin/env bash
# Rebuild assets/cm6/codemirror.js — a single-file IIFE bundle of
# the CodeMirror 6 modules the framework uses. Runs once per
# upgrade; the output is committed, no node_modules ships.
#
# CM6 is shipped as ESM-only modules with bare-specifier imports,
# so the browser cannot load it directly without a bundler. We
# call out to esbuild via npx (no global install needed) in a
# scratch tmpdir and copy just the resulting bundle into the
# tree. Pin versions in this script and in assets/README.note.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUT="${PROJECT_ROOT}/assets/cm6/codemirror.js"

WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT

cat > "${WORK}/entry.js" <<'JS'
// Bundle entry — re-exports CM6 modules on window.CM. Browser
// scripts call `CM.EditorView`, `CM.EditorState`, etc.
import {EditorState, Compartment, StateField, StateEffect}
    from "@codemirror/state";
import {EditorView, keymap, lineNumbers, highlightActiveLine,
        highlightActiveLineGutter, drawSelection, dropCursor,
        rectangularSelection, crosshairCursor, gutter, GutterMarker,
        Decoration, ViewPlugin, WidgetType}
    from "@codemirror/view";
import {defaultKeymap, history, historyKeymap, indentWithTab}
    from "@codemirror/commands";
import {searchKeymap, highlightSelectionMatches}
    from "@codemirror/search";
import {indentOnInput, syntaxHighlighting,
        defaultHighlightStyle, bracketMatching, foldGutter,
        foldKeymap}
    from "@codemirror/language";
import {autocompletion, completionKeymap, closeBrackets,
        closeBracketsKeymap}
    from "@codemirror/autocomplete";
import {linter, lintGutter, setDiagnostics, diagnosticCount}
    from "@codemirror/lint";
import {oneDark} from "@codemirror/theme-one-dark";

window.CM = {
  EditorState, EditorView, Compartment, StateField, StateEffect,
  keymap, lineNumbers, highlightActiveLine,
  highlightActiveLineGutter, drawSelection, dropCursor,
  rectangularSelection, crosshairCursor, gutter, GutterMarker,
  Decoration, ViewPlugin, WidgetType,
  defaultKeymap, history, historyKeymap, indentWithTab,
  searchKeymap, highlightSelectionMatches,
  indentOnInput, syntaxHighlighting, defaultHighlightStyle,
  bracketMatching, foldGutter, foldKeymap,
  autocompletion, completionKeymap, closeBrackets,
  closeBracketsKeymap,
  linter, lintGutter, setDiagnostics, diagnosticCount,
  oneDark,
};
JS

cd "${WORK}"
npm init -y >/dev/null
npm install --silent --no-save \
  @codemirror/state@6 @codemirror/view@6 \
  @codemirror/commands@6 @codemirror/search@6 \
  @codemirror/language@6 @codemirror/autocomplete@6 \
  @codemirror/lint@6 @codemirror/theme-one-dark@6 \
  esbuild@0.23

npx esbuild entry.js --bundle --format=iife --minify \
  --outfile=cm6.js

mkdir -p "$(dirname "${OUT}")"
cp cm6.js "${OUT}"
echo "wrote ${OUT} ($(stat -c%s "${OUT}") bytes)"
