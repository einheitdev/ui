// sidebar.js — pin/unpin behaviour for the left rail. Persists
// the pinned state to localStorage so subsequent page loads
// restore it. Hover-to-expand is CSS-only (`.sidebar:hover`),
// pin-to-stay-open is `[data-pinned="true"]` driven from here.
(function () {
  'use strict';
  const KEY = 'einheit:sidebar:pinned';
  const sidebar = document.querySelector('.sidebar');
  if (!sidebar) return;
  const pinBtn = sidebar.querySelector('.sidebar-pin');

  function setPinned(pinned) {
    sidebar.dataset.pinned = pinned ? 'true' : 'false';
    try {
      localStorage.setItem(KEY, pinned ? '1' : '0');
    } catch (e) {
      // localStorage unavailable (e.g. blocked-cookies or
      // private mode); the pin still works for the session.
    }
  }

  // Restore from previous session.
  let initial = false;
  try {
    initial = localStorage.getItem(KEY) === '1';
  } catch (e) {}
  setPinned(initial);

  if (pinBtn) {
    pinBtn.addEventListener('click', function (ev) {
      ev.preventDefault();
      ev.stopPropagation();
      setPinned(sidebar.dataset.pinned !== 'true');
    });
  }
})();
