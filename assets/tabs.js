// tabs.js — wires the partials/tabs strip. Click on a .tabs-tab
// flips .active on the strip's parent .tabs container's matching
// .tabs-panel. Body-level event delegation so newly swapped tab
// strips work without re-binding.
(function () {
  'use strict';
  if (window.__einheitTabs) return;
  window.__einheitTabs = true;

  document.body.addEventListener('click', function (ev) {
    const tab = ev.target.closest('.tabs-tab[data-tab-target]');
    if (!tab) return;
    const wrap = tab.closest('.tabs');
    if (!wrap) return;
    const slug = tab.dataset.tabTarget;

    wrap.querySelectorAll('.tabs-tab').forEach(function (t) {
      const on = t.dataset.tabTarget === slug;
      t.classList.toggle('active', on);
      t.setAttribute('aria-selected', on ? 'true' : 'false');
    });
    wrap.querySelectorAll(':scope > .tabs-panel,'
        + ':scope > .tabs-panels > .tabs-panel').forEach(
        function (p) {
          p.classList.toggle('active', p.dataset.tabSlug === slug);
        });
    wrap.dataset.active = slug;
  });
})();
