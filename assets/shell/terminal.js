// terminal.js — bridges xterm.js (or a textarea fallback) to the
// /shell/ws WebSocket endpoint. Loaded by adapters/shell/templates/
// shell/terminal.html.inja.
//
// Task 5 will vendor the xterm.js + fit-addon assets into
// /assets/xterm/ and tighten the theme glue (truecolor palette
// pulled from the framework's --einheit-* CSS vars). Today the
// script handles two states:
//
//   1. xterm.js loaded — render a real terminal.
//   2. xterm.js missing — fall back to a textarea so the WS bridge
//      is still smoke-testable end-to-end.
(function () {
  'use strict';

  const host = document.getElementById('terminal');
  if (!host) return;
  const wsPath = host.dataset.wsPath || '/shell/ws';
  const wsUrl = (location.protocol === 'https:' ? 'wss://' : 'ws://') +
                location.host + wsPath;
  const ws = new WebSocket(wsUrl);

  function send(payload) {
    if (ws.readyState === WebSocket.OPEN) {
      ws.send(payload);
    }
  }

  if (window.Terminal && !window.__einheitNoXterm) {
    // xterm.js path. Fit addon is optional — without it the
    // terminal renders at the host's nominal size and resize
    // events do not propagate.
    const term = new window.Terminal({
      cursorBlink: true,
      fontFamily: 'ui-monospace, "Cascadia Mono", "Source Code Pro", monospace',
      fontSize: 13,
      theme: {
        background: getCss('--einheit-bg', '#101010'),
        foreground: getCss('--einheit-fg', '#e0e0e0'),
      },
    });
    term.open(host);
    let fit = null;
    if (window.FitAddon && !window.__einheitNoFit) {
      fit = new window.FitAddon.FitAddon();
      term.loadAddon(fit);
      try { fit.fit(); } catch (e) { /* host not yet sized */ }
    }
    term.onData(send);

    function notifyResize() {
      const cols = term.cols;
      const rows = term.rows;
      send(JSON.stringify({type: 'resize', cols: cols, rows: rows}));
    }

    ws.addEventListener('open', notifyResize);
    ws.addEventListener('message', function (ev) {
      term.write(ev.data);
    });
    ws.addEventListener('close', function () {
      term.write('\r\n\x1b[31m[connection closed]\x1b[0m\r\n');
    });

    if (fit) {
      window.addEventListener('resize', function () {
        try { fit.fit(); } catch (e) { /* tear-down */ }
        notifyResize();
      });
    }
  } else {
    // Fallback: textarea + paragraph for output. Not pretty but
    // round-trips bytes, which is the point until xterm.js is
    // vendored.
    host.innerHTML = '';
    const out = document.createElement('pre');
    out.style.cssText =
        'background: var(--einheit-surface, #181818); ' +
        'color: var(--einheit-fg, #e0e0e0); ' +
        'padding: 12px; min-height: 360px; overflow:auto; ' +
        'font-family: monospace; white-space: pre-wrap;';
    const input = document.createElement('input');
    input.type = 'text';
    input.placeholder = 'type a command, Enter to send';
    input.style.cssText =
        'width: 100%; margin-top: 8px; padding: 8px; ' +
        'font-family: monospace; ' +
        'background: var(--einheit-bg, #101010); ' +
        'color: var(--einheit-fg, #e0e0e0); ' +
        'border: 1px solid var(--einheit-border, #333);';
    host.appendChild(out);
    host.appendChild(input);

    ws.addEventListener('message', function (ev) {
      out.textContent += ev.data;
      out.scrollTop = out.scrollHeight;
    });
    ws.addEventListener('close', function () {
      out.textContent += '\n[connection closed]\n';
    });
    input.addEventListener('keydown', function (ev) {
      if (ev.key === 'Enter') {
        send(input.value + '\r');
        input.value = '';
      }
    });
  }

  function getCss(name, fallback) {
    const v = getComputedStyle(document.documentElement)
                  .getPropertyValue(name).trim();
    return v || fallback;
  }
})();
