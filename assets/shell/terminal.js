// terminal.js — bridges xterm.js to the /shell/ws WebSocket.
//
// Resize protocol: the browser computes new (cols, rows) on every
// window resize via the fit-addon and sends a JSON envelope
// {"type":"resize","cols":N,"rows":M}. The adapter's WS handler
// recognizes leading-`{` text frames as control envelopes and
// forwards everything else as raw input to the PTY master.
//
// xterm theme palette is mapped from the framework's --einheit-*
// CSS variables on document.documentElement, so a theme.css
// change (or `theme use <name>` in the cli) re-themes the
// terminal on the next page load.
(function () {
  'use strict';

  const host = document.getElementById('terminal');
  if (!host || !window.Terminal) return;

  const wsPath = host.dataset.wsPath || '/shell/ws';
  const wsUrl = (location.protocol === 'https:' ? 'wss://' : 'ws://') +
                location.host + wsPath;

  function cssVar(name, fallback) {
    const v = getComputedStyle(document.documentElement)
                  .getPropertyValue(name).trim();
    return v || fallback;
  }

  // Map the framework's semantic palette onto xterm's 16-colour
  // ANSI slots. We don't have a 1:1 between (good/warn/bad/info)
  // and the ANSI palette, so we use the semantic colours for the
  // colours operators see most (red/green/yellow/blue) and copy
  // the bright variants from the same set. Black/white come from
  // the surface/foreground vars so they stay legible against the
  // chosen background.
  const palette = {
    background: cssVar('--einheit-bg', '#101010'),
    foreground: cssVar('--einheit-fg', '#e0e0e0'),
    cursor: cssVar('--einheit-accent', '#bb86fc'),
    cursorAccent: cssVar('--einheit-bg', '#101010'),
    selectionBackground: cssVar('--einheit-bg2', '#283238'),
    black: cssVar('--einheit-bg2', '#283238'),
    red: cssVar('--einheit-bad', '#cf6679'),
    green: cssVar('--einheit-good', '#03dac5'),
    yellow: cssVar('--einheit-warn', '#ffb74d'),
    blue: cssVar('--einheit-info', '#82aaff'),
    magenta: cssVar('--einheit-accent', '#bb86fc'),
    cyan: cssVar('--einheit-info', '#82aaff'),
    white: cssVar('--einheit-fg', '#e0e0e0'),
    brightBlack: cssVar('--einheit-fg3', '#7a8b94'),
    brightRed: cssVar('--einheit-bad', '#cf6679'),
    brightGreen: cssVar('--einheit-good', '#03dac5'),
    brightYellow: cssVar('--einheit-warn', '#ffb74d'),
    brightBlue: cssVar('--einheit-info', '#82aaff'),
    brightMagenta: cssVar('--einheit-accent', '#bb86fc'),
    brightCyan: cssVar('--einheit-info', '#82aaff'),
    brightWhite: cssVar('--einheit-fg', '#e0e0e0'),
  };

  const term = new window.Terminal({
    cursorBlink: true,
    fontFamily:
        'ui-monospace, "Cascadia Mono", "Source Code Pro", monospace',
    fontSize: 13,
    scrollback: 5000,
    allowProposedApi: true,
    theme: palette,
  });
  term.open(host);

  let fit = null;
  if (window.FitAddon && window.FitAddon.FitAddon) {
    fit = new window.FitAddon.FitAddon();
    term.loadAddon(fit);
  }

  // Run an initial fit once the container has settled. We delay
  // a tick to give the browser layout pass a chance to resolve
  // the host's height (the calc(100vh - …) uses viewport-derived
  // units so first-frame can race the resize observer).
  function tryFit() {
    if (!fit) return;
    try { fit.fit(); } catch (e) { /* host not yet sized */ }
  }
  requestAnimationFrame(tryFit);

  const ws = new WebSocket(wsUrl);
  ws.binaryType = 'arraybuffer';

  function send(payload) {
    if (ws.readyState === WebSocket.OPEN) {
      ws.send(payload);
    }
  }

  function notifyResize() {
    send(JSON.stringify({
      type: 'resize',
      cols: term.cols,
      rows: term.rows,
    }));
  }

  ws.addEventListener('open', function () {
    tryFit();
    notifyResize();
    term.focus();
  });

  ws.addEventListener('message', function (ev) {
    if (typeof ev.data === 'string') {
      term.write(ev.data);
    } else {
      // Binary frame — decode as UTF-8.
      term.write(new TextDecoder().decode(ev.data));
    }
  });

  ws.addEventListener('close', function () {
    term.write(
        '\r\n\x1b[31m[connection closed — reload to start a new session]'
        + '\x1b[0m\r\n');
  });

  term.onData(send);

  // ResizeObserver tracks the host element rather than the
  // window so we react to layout changes (sidebar toggles,
  // browser zoom) as well as raw window resizes.
  if (window.ResizeObserver) {
    const ro = new ResizeObserver(function () {
      tryFit();
      notifyResize();
    });
    ro.observe(host);
  } else {
    window.addEventListener('resize', function () {
      tryFit();
      notifyResize();
    });
  }
})();
