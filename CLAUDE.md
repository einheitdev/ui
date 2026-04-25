# CLAUDE.md

Guidance for Claude Code working in this repository.

## Project overview

`einheit-ui` is the shared C++ web UI framework every Einheit Networks
product's operator UI links against. Sibling of `einheit-cli`.

The framework is REST + server-rendered HTML fragments (HTMX) +
SSE for live updates. JavaScript on the wire stays under ~100 KB
gzipped. React was explicitly rejected on perf grounds.

Each product daemon owns its data and exposes it through Crow
routes; the framework supplies the template engine, theme, route
helpers, SSE bridge, static asset mount, and shared partials. Per
product an adapter under `adapters/<name>/` registers routes,
binds SSE topics, and contributes nav entries.

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
GoogleTest.

## Layout

- `include/einheit/ui/` — public framework headers
  - `error.h` — `Error<E>` template paired with `std::expected`
  - `theme.h` — semantic palette (mirrors cli/render/theme.h)
  - `route.h` — `Render`, `DetectFormat`, `RenderError`
  - `stream.h` — `EventStream`, `TopicBinding` (SSE bridge)
  - `server.h` — Crow bring-up + TLS
  - `static_files.h` — safe `/assets` mount
  - `adapter.h` — `ProductUiAdapter` contract
  - `render/template_engine.h` — inja wrapper
  - `render/escape.h` — HTML/attr/URL escapers
- `src/` — implementations mirror headers
- `templates/` — shared inja partials (`layout`, `partials/card`,
  `partials/table`, `partials/badge`, `partials/error`,
  `theme.css`)
- `assets/` — vendored client-side assets (htmx, sse extension,
  uplot, base.css). See `assets/README.note` for fetch commands.
- `adapters/example/` — minimal adapter to copy/paste from
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

- inja with auto-escape on. `{{ var }}` interpolation is HTML-safe;
  use `{{ var | safe }}` only when the value is itself rendered
  HTML (e.g. the `body` field passed into `layout`).
- Adapters resolve template names against their own dir first,
  then the framework dir. To override a framework partial, drop a
  same-named file in `adapters/<x>/templates/partials/`.
- Hot reload is on automatically in the debug preset; off in
  release.

## Live updates

- Default to SSE push, not HTMX polling. The framework's stance:
  having ditched SPA bloat, we can afford the more-engineered
  primitive.
- Bind once per topic at adapter `Mount` time, then `Publish` from
  whatever code mutates state. Each `Publish` renders the bound
  fragment and pushes one HTMX out-of-band swap to every connected
  browser.
- HTMX polling stays available (`hx-trigger="every 5s"`) for
  surfaces where push genuinely doesn't fit.

## Build anti-patterns

- **Never bring in npm.** No build step. Vendor JS with curl into
  `assets/`; the install rule picks it up.
- **No `// removed` comments, no unused `_foo` renames.** If code
  is gone, delete it.
- **Don't add a 2nd template engine.** If inja doesn't fit a
  case, fix it in inja or render the string with `RenderString`.
