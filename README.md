# einheit-ui

Shared C++ web UI framework for every Einheit Networks product.

REST + server-rendered HTML fragments (HTMX) + WebSocket for live
updates. Sibling of [einheit-cli](https://github.com/einheitdev/cli)
— one framework, many product adapters.

Licensed under the MIT License.

## Why not React?

Operator UIs have to feel snappy under mediocre management-LAN
conditions and stay debuggable with `curl`. SPA build pipelines,
virtual DOMs, and client-side stores aren't the right cost. The
trade is to do more work in the C++ server we already have, and
ship the smallest plausible JS payload: HTMX for hypermedia
interactions, uPlot for time-series, no build step.

## Build

```bash
sudo apt install -y libssl-dev pkg-config cmake ninja-build clang

# Release
cmake --preset default
cmake --build build --parallel

# Debug — enables template hot-reload from disk
cmake --preset debug
cmake --build build-debug --parallel

# Tests
ctest --preset default
```

CMake `FetchContent` pulls Crow, inja, nlohmann_json, spdlog,
CLI11, and GoogleTest. Vendor `htmx` + `uplot` once with the curl
commands in [`assets/README.note`](assets/README.note).

## Run

```bash
./build/einheit-ui --bind 127.0.0.1 --port 7542
```

Open <http://127.0.0.1:7542/> for the example dashboard. `POST /tick`
bumps the counter; a WebSocket push swaps the counter card in every
connected browser.

## Layout

- `include/einheit/ui/` — public framework headers
  - `route.h` — `Render`, `DetectFormat`, `RenderError`
  - `stream.h` — `EventStream` (WebSocket bridge, topic→fragment bindings)
  - `adapter.h` — `ProductUiAdapter` contract
  - `theme.h` — semantic palette → CSS custom properties
  - `server.h` — Crow bring-up + TLS
  - `render/template_engine.h` — inja wrapper with multi-root search
- `src/` — implementations mirror headers
- `templates/` — shared inja partials (`layout`, `partials/card`,
  `partials/table`, `partials/badge`, `partials/error`,
  `theme.css`)
- `assets/` — vendored client-side assets (HTMX, uPlot, base.css)
- `adapters/example/` — minimal adapter to copy/paste from
- `binaries/einheit-ui/` — server entry point
- `cmake/` — sub-library + third-party fetches

See [`CLAUDE.md`](CLAUDE.md) for code conventions.

## Three response shapes, one route

`Render(engine, req, args)` returns the right body based on the
incoming request:

| Caller       | Header / query                | Body                          |
|--------------|-------------------------------|-------------------------------|
| HTMX         | `HX-Request: true`            | Fragment HTML                 |
| API client   | `Accept: application/json`    | JSON                          |
| Browser      | neither                       | Full page (layout + fragment) |

Override explicitly with `?format=json|html|fragment`.

## Writing an adapter

An adapter is a `ProductUiAdapter` subclass with a templates
directory and route registrations:

```cpp
class MyAdapter : public einheit::ui::ProductUiAdapter {
 public:
  auto Slug() const -> std::string override { return "myprod"; }
  auto DisplayName() const -> std::string override {
    return "My Product";
  }
  auto TemplatesDir() const -> std::string override {
    return MYPROD_TEMPLATES_DIR;  // set by CMake
  }
  auto Nav() const -> std::vector<einheit::ui::NavEntry> override {
    return {{"/", "Home", "home"}};
  }

  auto Mount(einheit::ui::AdapterContext ctx) -> void override {
    ctx.events->Bind({
        .topic = "myprod.state",
        .fragment = "myprod/state",
        .swap_target = "state",
    });

    CROW_ROUTE(*ctx.app, "/")(
        [eng = ctx.templates](const crow::request &req) {
          einheit::ui::RenderArgs args{
              .fragment = "myprod/home",
              .data = {{"hello", "world"}}};
          auto r = einheit::ui::Render(*eng, req, args);
          return r ? *r
                   : einheit::ui::RenderError(*eng, req, 500,
                                              "render",
                                              r.error().message);
        });
  }
};
```

The framework prepends the adapter's `TemplatesDir()` to the
template engine's search path, so a same-named file under
`adapters/myprod/templates/partials/card.html.inja` shadows the
framework partial.

## Live updates

Default to WebSocket push; polling stays available as an escape
hatch. The server exposes a single `/events` WebSocket; HTMX's
`htmx-ext-ws` extension consumes the messages and applies the
out-of-band swap.

```cpp
// At Mount() time:
ctx.events->Bind({
    .topic = "f.rules.changed",
    .fragment = "f/rules_table",
    .swap_target = "rules",
});

// On state change anywhere in the daemon:
ctx.events->Publish("f.rules.changed",
                    {{"rules", current_rules}});
```

The framework renders the fragment, wraps it for HTMX out-of-band
swap, and broadcasts to every connected browser.

## Themes

Six built-in palettes, selected with `--theme`:

| Name             | Notes                                  |
|------------------|----------------------------------------|
| `psychotropic`   | default dark; matches the cli theme    |
| `light`          | mirror palette for light terminals     |
| `ocean`          | cool blue-teal                         |
| `forest`         | earthy green                           |
| `solarized-dark` | Schoonover's Solarized                 |
| `high-contrast`  | near-monochrome, max legibility        |

Each emits a CSS custom-property block at `/theme.css` keyed by
`--einheit-bg` / `--einheit-fg` / `--einheit-good` / etc. that
the layout includes. Adapters can ship their own
`templates/theme.css.inja` to override.

## Status

Skeleton stage, walking-skeleton complete.

Working: template engine with multi-root search and hot reload,
route format detection, themed CSS with six named palettes,
static asset mount with path-traversal guards, WebSocket-backed
`EventStream` with topic→fragment bindings and OOB-swap
broadcast, the partials product UIs need (card, table, badge,
status, log_entry, button, empty, error), example adapter
exercising every response shape plus a live tick, 71 unit tests.

TODO: per-product time-series chart partial (uPlot) — the
WebSocket-vs-poll choice for chart data deserves its own pass.
End-to-end integration tests against a running Crow server.
First real product adapter (`f` rules table, `hd` peers panel).
