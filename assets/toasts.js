// toasts.js — wires the #toasts stack populated by
// EventStream::PublishToast. Each new toast is swapped in by
// htmx-ws's OOB handler; we observe the stack via MutationObserver,
// auto-dismiss after the partial's data-toast-life timeout, and
// hook the close button.
(function () {
  'use strict';
  if (window.__einheitToasts) return;
  window.__einheitToasts = true;

  const stack = document.getElementById('toasts');
  if (!stack) return;

  function dismiss(t) {
    if (!t || !t.isConnected) return;
    t.classList.add('toast-leaving');
    setTimeout(function () {
      if (t.isConnected) t.remove();
    }, 250);
  }

  function setup(t) {
    if (!t || t.dataset.bound === '1') return;
    t.dataset.bound = '1';
    const life = parseInt(t.dataset.toastLife || '4500', 10);
    setTimeout(function () { dismiss(t); }, life);
    const close = t.querySelector('.toast-close');
    if (close) {
      close.addEventListener('click', function () { dismiss(t); });
    }
  }

  // Anything already in the stack at script-load (from a hard
  // reload that landed a server-pushed toast before this script
  // ran) gets wired retroactively.
  stack.querySelectorAll('.toast').forEach(setup);

  new MutationObserver(function (records) {
    records.forEach(function (r) {
      r.addedNodes.forEach(function (n) {
        if (!n.classList) return;
        if (n.classList.contains('toast')) setup(n);
        // OOB swap can deposit a wrapping div with toasts inside
        // — recurse one level.
        n.querySelectorAll && n.querySelectorAll('.toast').forEach(setup);
      });
    });
  }).observe(stack, {childList: true, subtree: true});
})();
