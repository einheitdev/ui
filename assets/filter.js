// filter.js — live client-side filter for the partials/filter
// input. Reads data-filter-target, hides rows that don't
// contain the typed substring (case-insensitive), and updates
// the optional data-filter-counter element with "X of Y".
(function () {
  'use strict';
  if (window.__einheitFilter) return;
  window.__einheitFilter = true;

  function update(input) {
    const sel = input.dataset.filterTarget;
    if (!sel) return;
    const needle = input.value.trim().toLowerCase();
    const rows = document.querySelectorAll(sel);
    let shown = 0;
    rows.forEach(function (row) {
      const hay = row.textContent.toLowerCase();
      const match = !needle || hay.includes(needle);
      row.style.display = match ? '' : 'none';
      if (match) shown++;
    });
    const counterId = input.dataset.filterCounter;
    if (counterId) {
      const c = document.getElementById(counterId);
      if (c) {
        c.textContent = needle ? shown + ' of ' + rows.length
                                : String(rows.length);
      }
    }
  }

  document.body.addEventListener('input', function (ev) {
    if (!ev.target.classList.contains('filter-input')) return;
    update(ev.target);
  });

  // After every page swap (and once at load), re-apply any
  // already-typed filter so the UI doesn't show all rows when
  // the user navigated away and came back to a stored input
  // value (browsers preserve type=search across hx-boost).
  function applyAll() {
    document.querySelectorAll('.filter-input').forEach(function (i) {
      if (i.value) update(i);
    });
  }
  applyAll();
  document.body.addEventListener('htmx:afterSettle', applyAll);
})();
