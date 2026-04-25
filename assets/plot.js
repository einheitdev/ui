// plot.js — boots uPlot for every <figure class="plot"> on the
// page and routes WebSocket data points from /metrics/ws into the
// matching chart. The framework's stream layer publishes
// {"type":"data","topic":<t>,"point":<json>} envelopes; this
// script reads each figure's data-* attributes for config and
// hydrates with the historic ring-buffer dump baked into
// data-points.
(function () {
  'use strict';

  if (typeof uPlot === 'undefined') return;

  function cssVar(name, fallback) {
    const v = getComputedStyle(document.documentElement)
                  .getPropertyValue(name).trim();
    return v || fallback;
  }

  // Map the framework's semantic colour vars onto stroke colours.
  // Keep this in sync with what the template's `series.semantic`
  // values are documented to accept.
  const SEMANTIC = {
    good: '--einheit-good',
    warn: '--einheit-warn',
    bad: '--einheit-bad',
    info: '--einheit-info',
    accent: '--einheit-accent',
    fg: '--einheit-fg2',
  };

  function colorOf(semantic) {
    const v = SEMANTIC[semantic] || SEMANTIC.info;
    return cssVar(v, '#888');
  }

  // Convert the historic [[ts,y0,y1,...], ...] array we get from
  // the server's ring buffer into uPlot's column-major layout
  // [[ts...], [y0...], [y1...], ...].
  function toColumns(rows, seriesCount) {
    const cols = [[]];
    for (let i = 0; i < seriesCount; i++) cols.push([]);
    for (const row of rows) {
      cols[0].push(row[0]);
      for (let i = 0; i < seriesCount; i++) {
        cols[i + 1].push(row[i + 1] ?? null);
      }
    }
    return cols;
  }

  function trimByWindow(cols, windowS) {
    if (!windowS || cols[0].length === 0) return cols;
    const cutoff = cols[0][cols[0].length - 1] - windowS;
    let drop = 0;
    while (drop < cols[0].length && cols[0][drop] < cutoff) drop++;
    if (drop === 0) return cols;
    for (let i = 0; i < cols.length; i++) cols[i] = cols[i].slice(drop);
    return cols;
  }

  // ------------------------------------------------------------
  // Set up every plot host on the page.
  // ------------------------------------------------------------

  const figures = Array.from(document.querySelectorAll(
      'figure.plot[data-topic]'));
  if (figures.length === 0) return;

  const byTopic = new Map();

  for (const fig of figures) {
    const host = fig.querySelector('.plot-host');
    if (!host) continue;

    const topic = fig.dataset.topic;
    const windowS = parseInt(fig.dataset.windowS || '300', 10);
    const yLabel = fig.dataset.yLabel || '';
    let series, points;
    try {
      series = JSON.parse(fig.dataset.series || '[]');
      points = JSON.parse(fig.dataset.points || '[]');
    } catch (e) {
      console.error('plot.js: bad config on', fig, e);
      continue;
    }

    const seriesCfg = [
      // Index 0 is the x-axis (timestamp seconds).
      {label: 'time'},
      ...series.map(function (s) {
        return {
          label: s.label,
          stroke: colorOf(s.semantic),
          width: 1.5,
          points: {show: false},
        };
      }),
    ];

    let data = toColumns(points, series.length);

    const opts = {
      width: host.clientWidth,
      height: host.clientHeight || 180,
      legend: {show: true, live: true},
      scales: {x: {time: true}},
      axes: [
        {stroke: cssVar('--einheit-fg3', '#888'),
         grid: {stroke: cssVar('--einheit-border', '#333'),
                width: 1}},
        {stroke: cssVar('--einheit-fg3', '#888'),
         label: yLabel,
         labelGap: 4,
         grid: {stroke: cssVar('--einheit-border', '#333'),
                width: 1}},
      ],
      series: seriesCfg,
    };

    const u = new uPlot(opts, data, host);
    byTopic.set(topic, {u: u, data: data, windowS: windowS,
                        seriesCount: series.length});

    // Resize observer keeps the chart filling its host as the
    // sidebar pin/expand changes the page width.
    if (window.ResizeObserver) {
      const ro = new ResizeObserver(function () {
        u.setSize({width: host.clientWidth,
                   height: host.clientHeight || 180});
      });
      ro.observe(host);
    }
  }

  // ------------------------------------------------------------
  // Live updates.
  // ------------------------------------------------------------

  const wsUrl = (location.protocol === 'https:' ? 'wss://' : 'ws://') +
                location.host + '/metrics/ws';
  const ws = new WebSocket(wsUrl);

  ws.addEventListener('message', function (ev) {
    let msg;
    try { msg = JSON.parse(ev.data); }
    catch (e) { return; }
    if (msg.type !== 'data' || !msg.topic) return;
    const slot = byTopic.get(msg.topic);
    if (!slot) return;
    const point = msg.point;
    slot.data[0].push(point[0]);
    for (let i = 0; i < slot.seriesCount; i++) {
      slot.data[i + 1].push(point[i + 1] ?? null);
    }
    slot.data = trimByWindow(slot.data, slot.windowS);
    slot.u.setData(slot.data);
  });
})();
