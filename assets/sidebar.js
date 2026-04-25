// sidebar.js — pin/unpin behaviour for the left rail and the
// settings group's expand-and-pick UX. Persists three preferences
// (theme, scale, lang) to cookies + localStorage so server-side
// rendering can read the cookie and the next page load applies
// the choice without flashing the default.
(function () {
  'use strict';

  const PIN_KEY = 'einheit:sidebar:pinned';
  const sidebar = document.querySelector('.sidebar');
  if (!sidebar) return;
  const pinBtn = sidebar.querySelector('.sidebar-pin');
  const root = document.documentElement;

  // ----------------------------------------------------------
  // Hover tracker. CSS :hover doesn't survive hx-boost — when
  // the body swaps, the browser doesn't re-evaluate :hover on
  // the new sidebar element until the next mousemove, so the
  // rail collapses and then animates back open. We mirror the
  // hover state to html[data-sidebar-hovering] via a global
  // pointer tracker so it persists across swaps.
  //
  // The listener is deduped via a window flag because sidebar.js
  // re-runs on every boosted nav. The tracker uses a sticky
  // X-band: enter when cursor x < 4rem, leave when x > 17.5rem.
  // ----------------------------------------------------------
  if (!window.__einheitSidebarHover) {
    window.__einheitSidebarHover = true;
    const COLLAPSED = 64;     // 4rem
    const EXPANDED = 280;     // 17.5rem
    document.addEventListener('mousemove', function (ev) {
      const expanded = root.dataset.sidebarHovering === 'true';
      const x = ev.clientX;
      const should = expanded ? x < EXPANDED : x < COLLAPSED;
      if (should === expanded) return;
      if (should) {
        root.dataset.sidebarHovering = 'true';
      } else {
        delete root.dataset.sidebarHovering;
      }
    });
    document.addEventListener('mouseleave', function () {
      delete root.dataset.sidebarHovering;
    });
  }

  // ----------------------------------------------------------
  // Pin / unpin. State lives on <html data-sidebar-pinned> so a
  // render-blocking inline <script> in <head> can apply it
  // before the body parses — no width animation flash on
  // hx-boost'd page swaps.
  // ----------------------------------------------------------

  function isPinned() {
    return root.dataset.sidebarPinned === 'true';
  }

  function setPinned(pinned) {
    if (pinned) {
      root.dataset.sidebarPinned = 'true';
    } else {
      delete root.dataset.sidebarPinned;
    }
    try {
      localStorage.setItem(PIN_KEY, pinned ? '1' : '0');
    } catch (e) { /* private mode */ }
  }

  // The inline head script already applied the persisted choice
  // synchronously. We just need to wire interaction handlers.

  if (pinBtn) {
    pinBtn.addEventListener('click', function (ev) {
      ev.preventDefault();
      ev.stopPropagation();
      setPinned(!isPinned());
    });
  }

  sidebar.addEventListener('click', function (ev) {
    if (ev.target.closest(
            'a, button, input, textarea, select, label')) {
      return;
    }
    setPinned(!isPinned());
  });

  // ----------------------------------------------------------
  // Settings group + value pickers.
  // ----------------------------------------------------------

  function toggleAttr(el) {
    el.dataset.open = el.dataset.open === 'true' ? 'false' : 'true';
  }

  // Top-level "Settings" group toggle.
  const group = sidebar.querySelector('.sidebar-group');
  if (group) {
    const head = group.querySelector('.sidebar-group-head');
    head.addEventListener('click', function (ev) {
      ev.stopPropagation();
      toggleAttr(group);
    });
  }

  // Each value picker (theme/scale/lang) opens its own submenu.
  // Closing one when another opens isn't enforced — Relay lets
  // multiple stay open and we follow the same pattern.
  sidebar.querySelectorAll('.sidebar-pick').forEach(function (pick) {
    const head = pick.querySelector('.sidebar-pick-head');
    if (!head) return;
    head.addEventListener('click', function (ev) {
      ev.stopPropagation();
      toggleAttr(pick);
    });
  });

  // ----------------------------------------------------------
  // Persisting + applying preferences.
  // ----------------------------------------------------------

  function setCookie(name, value, days) {
    const max = days * 24 * 60 * 60;
    document.cookie = name + '=' + encodeURIComponent(value) +
        '; path=/; max-age=' + max + '; SameSite=Lax';
  }

  function getCookie(name) {
    const m = document.cookie.match(
        new RegExp('(?:^|; )' + name + '=([^;]*)'));
    return m ? decodeURIComponent(m[1]) : '';
  }

  // Scale: stored as a numeric string (0.875 / 1 / 1.125), applied
  // by setting --einheit-scale on <html>.
  const SCALE_LABELS = {
    '0.875': 'small',
    '1': 'medium',
    '1.125': 'large',
  };
  function applyScale(value) {
    document.documentElement.style.setProperty(
        '--einheit-scale', value);
    const label = SCALE_LABELS[value] || 'medium';
    sidebar.querySelectorAll('[data-current="scale"]').forEach(
        function (el) { el.textContent = label; });
    sidebar.querySelectorAll('[data-set="scale"]').forEach(
        function (el) {
          el.setAttribute('aria-current',
              el.dataset.value === value ? 'true' : 'false');
        });
  }

  // Theme: stored as the name (e.g. "ocean"). Applied by swapping
  // the /theme.css link's href so the server returns the new
  // palette. Server-side, the route reads the einheit_theme
  // cookie to pick the palette.
  function applyTheme(name) {
    const link = document.querySelector(
        'link[rel="stylesheet"][href^="/theme.css"]');
    if (link) {
      link.href = '/theme.css?_=' + Date.now();
    }
    sidebar.querySelectorAll('[data-current="theme"]').forEach(
        function (el) { el.textContent = name; });
    sidebar.querySelectorAll('[data-set="theme"]').forEach(
        function (el) {
          el.setAttribute('aria-current',
              el.dataset.value === name ? 'true' : 'false');
        });
  }

  // Language: stored as a 2-letter code. The framework has no
  // translation pipeline yet, so this only persists the choice —
  // adapter strings stay in their authored language until i18n
  // lands. The picker UI is wired so the plumbing is ready when
  // it does.
  function applyLang(code) {
    sidebar.querySelectorAll('[data-current="lang"]').forEach(
        function (el) {
          el.textContent = (code || 'en').toUpperCase();
        });
    sidebar.querySelectorAll('[data-set="lang"]').forEach(
        function (el) {
          el.setAttribute('aria-current',
              el.dataset.value === code ? 'true' : 'false');
        });
    document.documentElement.setAttribute('lang', code || 'en');
  }

  // Wire option clicks. The data-set / data-value pair on each
  // .sidebar-option button identifies which preference + value to
  // apply.
  sidebar.querySelectorAll('.sidebar-option').forEach(function (opt) {
    opt.addEventListener('click', function (ev) {
      ev.stopPropagation();
      const key = opt.dataset.set;
      const value = opt.dataset.value;
      if (!key || value === undefined) return;
      if (key === 'theme') {
        setCookie('einheit_theme', value, 365);
        applyTheme(value);
      } else if (key === 'scale') {
        setCookie('einheit_scale', value, 365);
        applyScale(value);
      } else if (key === 'lang') {
        setCookie('einheit_lang', value, 365);
        applyLang(value);
      }
    });
  });

  // Restore from cookie on every page load — the server may also
  // honour the cookie (theme.css does), this keeps client-side
  // state (label text, aria-current, --einheit-scale) coherent.
  applyTheme(getCookie('einheit_theme') || 'psychotropic');
  applyScale(getCookie('einheit_scale') || '1');
  applyLang(getCookie('einheit_lang') || 'en');
})();
