// copy.js — wires the .copy-btn partial. Click reads the value
// from data-copy-value (or the text content of data-copy-selector)
// and writes it to the clipboard, then flashes a check icon for
// a moment as feedback.
//
// Body uses hx-target=main.app-main, so this script lives at
// body bottom and runs once. Event delegation on the body picks
// up buttons in newly-swapped main content without re-binding.
(function () {
  'use strict';
  if (window.__einheitCopy) return;
  window.__einheitCopy = true;

  document.body.addEventListener('click', function (ev) {
    const btn = ev.target.closest('.copy-btn');
    if (!btn) return;
    ev.preventDefault();

    let text = btn.dataset.copyValue || '';
    if (!text && btn.dataset.copySelector) {
      const src = document.querySelector(btn.dataset.copySelector);
      if (src) text = src.textContent.trim();
    }
    if (!text) return;

    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(text).then(showDone(btn));
    } else {
      // Old-browser fallback: textarea + execCommand.
      const ta = document.createElement('textarea');
      ta.value = text;
      ta.style.position = 'fixed';
      ta.style.opacity = '0';
      document.body.appendChild(ta);
      ta.focus();
      ta.select();
      try { document.execCommand('copy'); } catch (e) {}
      document.body.removeChild(ta);
      showDone(btn)();
    }
  });

  function showDone(btn) {
    return function () {
      btn.classList.add('copy-done');
      setTimeout(function () { btn.classList.remove('copy-done'); },
                 1200);
    };
  }
})();
