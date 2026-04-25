// time_range.js — flips the .active class among .time-range-opt
// siblings on click. Sits alongside htmx, which is still doing
// the hx-get fetch + swap; this is just the visual toggle so
// the strip stays correct independent of swap success.
(function () {
  'use strict';
  if (window.__einheitTimeRange) return;
  window.__einheitTimeRange = true;

  document.body.addEventListener('click', function (ev) {
    const opt = ev.target.closest('.time-range-opt');
    if (!opt) return;
    const wrap = opt.closest('.time-range');
    if (!wrap) return;
    wrap.querySelectorAll('.time-range-opt').forEach(function (o) {
      o.classList.toggle('active', o === opt);
    });
  });
})();
