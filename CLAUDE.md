# CLAUDE.md

Guidance for Claude Code working in this repository.

## Project overview

`einheit-ui` is the shared C++ web UI framework every Einheit Networks
product's operator UI links against. Sibling of `einheit-cli`.

The framework is REST + server-rendered HTML fragments (HTMX) +
WebSocket for live updates. JavaScript on the wire stays under
~100 KB gzipped. React was explicitly rejected on perf grounds.

Each product daemon owns its data and exposes it through Crow
routes; the framework supplies the template engine, theme, route
helpers, WebSocket bridge, static asset mount, and shared
partials. Per product an adapter under `adapters/<name>/`
registers routes, binds WS topics, and contributes nav entries.

Two adapter shapes are in use today: in-process (the `example`
adapter holds its own state) and HTTP-proxy (the `hd_relay`
adapter calls the daemon's existing `/api/v1/...` endpoints over
cpp-httplib and renders the JSON it gets back). Pick the
former when the UI binary IS the daemon binary; pick the latter
when the daemon already exposes a stable REST API and the UI
runs as a sidecar.

## Build

```bash
cmake --preset default
cmake --build build --parallel

# Debug — enables template hot-reload from disk.
cmake --preset debug
cmake --build build-debug --parallel

# Tests
ctest --preset default
```

System dependencies (install before `cmake`):

```bash
sudo apt install -y \
  libssl-dev pkg-config cmake ninja-build clang
```

FetchContent pulls: Crow, inja, nlohmann_json, spdlog, CLI11,
cpp-httplib (for adapter HTTP clients), GoogleTest.

## Layout

- `include/einheit/ui/` — public framework headers
  - `error.h` — `Error<E>` template paired with `std::expected`
  - `theme.h` — semantic palette (mirrors cli/render/theme.h)
  - `route.h` — `Render`, `DetectFormat`, `RenderError`
  - `stream.h` — `EventStream`, `TopicBinding` (WebSocket bridge)
  - `server.h` — Crow bring-up + TLS
  - `static_files.h` — safe `/assets` mount
  - `adapter.h` — `ProductUiAdapter` contract
  - `render/template_engine.h` — inja wrapper
  - `render/escape.h` — HTML/attr/URL escapers
- `src/` — implementations mirror headers
- `templates/` — shared inja partials (`layout`, `partials/card`,
  `partials/table`, `partials/badge`, `partials/error`,
  `partials/status`, `partials/log_entry`, `partials/button`,
  `partials/empty`, `theme.css`)
- `assets/` — vendored client-side assets (htmx, ws extension,
  uplot, base.css). See `assets/README.note` for fetch commands.
- `adapters/example/` — minimal adapter to copy/paste from
- `adapters/hd_relay/` — Hyper-DERP UI; HTTP-proxies the daemon's
  existing `/api/v1/...` endpoints via `cpp-httplib`
- `binaries/einheit-ui/src/main.cc` — server entry point
- `cmake/` — sub-library definitions + third-party fetches

## Code conventions

- **C++23**, Google Style Guide, 80-char lines, 2-space indent.
- `clang` (not gcc) selected in `CMakeLists.txt:5-19`.
- **Trailing return types** on every function:
  `auto Foo() -> std::expected<T, Error<E>>`.
- **Data-oriented**: plain structs for data; free functions
  operate on references. Only `ProductUiAdapter` has virtuals
  (cross-module contract; same rationale as cli's `Transport`).
- **Error handling**: `std::expected<T, Error<E>>` everywhere.
  Declare a module-local `enum class FooError` and pair with
  `Error<>`.
- **Namespaces**: `einheit::ui::`, `einheit::ui::render::`.
  Adapters use `einheit::adapters::<name>::`.
- **Doxygen**: `/// @file`, `/// @brief`, `///` on every public
  header member. Use `///`, never `/** */`.
- Include guards: `INCLUDE_EINHEIT_UI_<MODULE>_<FILE>_H_`.
- Place comments on the line above the code, never on the same
  line.

## Templating

- inja with auto-escape on. `{{ var }}` interpolation is HTML-safe.
  inja has no `safe` filter, so the framework injects rendered
  HTML bodies via the `<!--EINHEIT_BODY-->` placeholder
  substitution in the layout, not via interpolation.
- Adapters resolve template names against their own dir first,
  then the framework dir. To override a framework partial, drop a
  same-named file in `adapters/<x>/templates/partials/`.
- Hot reload is on automatically in the debug preset; off in
  release.

### Partial gotchas

- inja raises on missing-variable access in `{% if x %}` (no
  Jinja-style `is defined`). For optional fields on a loop var,
  use `{% if existsIn(loopvar, "key") %}`. For top-level optional
  vars, the framework's `Render()` backfills the layout's
  `meta.{title,brand,active,nav}` with defaults.
- inja string literals in pipe filters need **double quotes**:
  `default("info")`, not `default('info')`.
- inja `{% for k, v in obj %}` requires `obj` to be an object,
  not null. From C++, write `nlohmann::json::object()` to get an
  empty object — the brace-init `{{"k", {}}}` collapses to null.

## Live updates

- Default to WebSocket push, not HTMX polling. The framework's
  stance: having ditched SPA bloat, we can afford the more-
  engineered primitive.
- The server exposes one WebSocket at `/events`. HTMX's `ws-ext`
  consumes messages and performs OOB swaps; the framework formats
  every Publish as `<div hx-swap-oob="strategy:#target">...</div>`
  via `BuildOobWrapper`.
- We picked WebSocket over SSE because Crow's response model
  buffers the body until `end()` and has no flush primitive — true
  SSE would require a Crow fork. WebSocket is first-class in Crow
  via `CROW_WEBSOCKET_ROUTE`.
- Bind once per topic at adapter `Mount` time, then `Publish` from
  whatever code mutates state.
- HTMX polling stays available (`hx-trigger="every 5s"`) for
  surfaces where push genuinely doesn't fit.

## Build anti-patterns

- **Never bring in npm.** No build step. Vendor JS with curl into
  `assets/`; the install rule picks it up.
- **No `// removed` comments, no unused `_foo` renames.** If code
  is gone, delete it.
- **Don't add a 2nd template engine.** If inja doesn't fit a
  case, fix it in inja or render the string with `RenderString`.
