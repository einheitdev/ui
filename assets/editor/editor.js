// editor.js — boots CodeMirror 6 inside #editor-source. The CM6
// modules are exposed on `window.CM` by /assets/cm6/codemirror.js.
//
// Today this is the minimal "basic-setup-equivalent" config —
// line numbers, history, search, fold gutter, autocomplete,
// bracket matching, lint gutter, and the one-dark theme. Per-
// language extensions, server-pushed diagnostics, and the
// analysis-pane round-trip plug in here as the FWL surface
// matures.
(function () {
  'use strict';
  const host = document.getElementById('editor-source');
  if (!host || !window.CM) return;
  const CM = window.CM;

  const starter = host.dataset.starterText || '';
  // Lang hint is unused by the framework today — products that
  // ship a Lezer grammar dispatch on this attribute themselves.
  const lang = host.dataset.starterLang || '';

  // Compose the basic-setup-equivalent. We assemble the
  // extensions array by hand rather than pulling @codemirror/
  // basic-setup so the bundle is exactly what the framework
  // uses, no drag-along.
  const extensions = [
    CM.lineNumbers(),
    CM.highlightActiveLineGutter(),
    CM.foldGutter(),
    CM.drawSelection(),
    CM.dropCursor(),
    CM.EditorState.allowMultipleSelections.of(true),
    CM.indentOnInput(),
    CM.bracketMatching(),
    CM.closeBrackets(),
    CM.autocompletion(),
    CM.rectangularSelection(),
    CM.crosshairCursor(),
    CM.highlightActiveLine(),
    CM.highlightSelectionMatches(),
    CM.syntaxHighlighting(CM.defaultHighlightStyle, {fallback: true}),
    CM.lintGutter(),
    CM.history(),
    CM.keymap.of([
      ...CM.closeBracketsKeymap,
      ...CM.defaultKeymap,
      ...CM.searchKeymap,
      ...CM.historyKeymap,
      ...CM.foldKeymap,
      ...CM.completionKeymap,
      CM.indentWithTab,
    ]),
    CM.oneDark,
    CM.EditorView.lineWrapping,
  ];

  const view = new CM.EditorView({
    state: CM.EditorState.create({doc: starter, extensions: extensions}),
    parent: host,
  });

  // Expose the live view + the CM module for any per-product
  // bootstrap script that wants to add language extensions or
  // dispatch server-side diagnostics.
  window.einheitEditor = {view: view, CM: CM, lang: lang};
})();
