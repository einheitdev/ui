cmake_minimum_required(VERSION 3.20...4.20)

# ============================================================================
# Compiler Selection — MUST be before project()
# ============================================================================
if(NOT CMAKE_CROSSCOMPILING
    AND NOT DEFINED CMAKE_C_COMPILER
    AND NOT DEFINED CMAKE_CXX_COMPILER)
  find_program(_clang clang)
  find_program(_clangpp clang++)
  if(_clang AND _clangpp)
    set(CMAKE_C_COMPILER "${_clang}"
      CACHE FILEPATH "C compiler" FORCE)
    set(CMAKE_CXX_COMPILER "${_clangpp}"
      CACHE FILEPATH "C++ compiler" FORCE)
  endif()
  unset(_clang CACHE)
  unset(_clangpp CACHE)
endif()

project(einheit-ui
  LANGUAGES C CXX
  DESCRIPTION "Einheit Networks shared web UI framework"
  VERSION 0.0.1
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Detect and select fastest available ELF linker.
if(UNIX)
  set(_found_linker FALSE)
  foreach(_ld mold lld gold)
    execute_process(
      COMMAND "${CMAKE_COMMAND}" -E env LC_ALL=C which ${_ld}
      OUTPUT_VARIABLE _path
      RESULT_VARIABLE _res
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(_res EQUAL 0 AND _path)
      execute_process(
        COMMAND "${_path}" --version
        RESULT_VARIABLE _ld_works
        OUTPUT_QUIET ERROR_QUIET
      )
      if(_ld_works EQUAL 0)
        message(STATUS "Using linker: ${_ld} at ${_path}")
        set(CMAKE_EXE_LINKER_FLAGS
          "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=${_ld}")
        set(CMAKE_SHARED_LINKER_FLAGS
          "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=${_ld}")
        set(CMAKE_MODULE_LINKER_FLAGS
          "${CMAKE_MODULE_LINKER_FLAGS} -fuse-ld=${_ld}")
        set(_found_linker TRUE)
        break()
      endif()
    endif()
  endforeach()
endif()

if(CMAKE_BUILD_TYPE MATCHES Debug)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_C_FLAGS_DEBUG
      "${CMAKE_C_FLAGS_DEBUG} -O0 -g -fno-omit-frame-pointer")
    set(CMAKE_CXX_FLAGS_DEBUG
      "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g -fno-omit-frame-pointer")
  endif()
endif()

# AddressSanitizer + UndefinedBehaviorSanitizer build. Catches the
# crash classes try/catch cannot — null derefs, use-after-free on the
# WebSocket connection lifecycle, buffer overflows, integer UB — before
# they ship. The whole test suite is expected to pass clean under it;
# this is part of the hardening gate. Enable with the `asan` preset or
# -DEINHEIT_UI_SANITIZE=ON.
option(EINHEIT_UI_SANITIZE
  "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
if(EINHEIT_UI_SANITIZE)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
      -fsanitize=address,undefined
      -fno-sanitize-recover=all
      -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
    message(STATUS "Sanitizers ON: address,undefined")
  else()
    message(WARNING
      "EINHEIT_UI_SANITIZE requested but compiler is not GNU/Clang")
  endif()
endif()

message(STATUS "C compiler:   ${CMAKE_C_COMPILER}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER}")

option(EINHEIT_UI_BUILD_TESTS    "Build the test suite"      ON)
option(EINHEIT_UI_BUILD_ADAPTERS "Build the product adapters" ON)
option(EINHEIT_UI_BUILD_BINARY   "Build the einheit-ui binary" ON)
option(EINHEIT_UI_TEMPLATE_HOT_RELOAD
  "Reload templates from disk on every render (debug only)" OFF)

# Management-plane step 3: the UI drives the CLI command engine. When ON,
# the UI links the sibling einheit-cli as a library so every *mutating* UI
# action runs through the same `engine::Execute` chokepoint the shell uses
# (one audit trail, one auth path). Enables the command-driver seam
# (`einheit_ui_command`) and the confd reference adapter.
option(EINHEIT_UI_WITH_ENGINE
  "Link the sibling einheit-cli command engine (confd adapter + seam)" ON)
set(EINHEIT_UI_CLI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../cli"
  CACHE PATH "Path to the einheit-cli source tree (the command engine)")

# Where templates live at runtime. Default: alongside the binary; the
# adapter can override per-product.
set(EINHEIT_UI_TEMPLATES_DIR
  "${CMAKE_INSTALL_PREFIX}/share/einheit-ui/templates"
  CACHE PATH
  "Default runtime path for shared template files")

include("cmake/3rd_party_libs/fetch.cmake")
include("cmake/lib_ui_render.cmake")
include("cmake/lib_ui_core.cmake")

# ---------------------------------------------------------------------------
# Command-engine seam (management-plane step 3: "the UI drives the CLI").
# cli is consumed read-only as a library; never modified here. Ordering is
# load-bearing: this comes AFTER ui's fetch.cmake so ui's FetchContent
# declarations of the shared deps (spdlog / CLI11 / nlohmann / googletest)
# win, and cli's own `if(NOT TARGET ...)`-guarded fetches dedup against them
# instead of redeclaring the same targets.
# ---------------------------------------------------------------------------
set(EINHEIT_UI_HAVE_ENGINE OFF)
if(EINHEIT_UI_WITH_ENGINE)
  if(EXISTS "${EINHEIT_UI_CLI_DIR}/CMakeLists.txt")
    # cli is a build-only dependency here: no cli test suite (mirrors the
    # peer workspaces), but keep its adapters — the cli binary links them.
    set(EINHEIT_BUILD_TESTS OFF CACHE BOOL "cli tests off under ui" FORCE)
    add_subdirectory("${EINHEIT_UI_CLI_DIR}" "${CMAKE_BINARY_DIR}/cli")
    include("cmake/lib_ui_command.cmake")
    set(EINHEIT_UI_HAVE_ENGINE ON)
    message(STATUS
      "Command engine: linking einheit-cli from ${EINHEIT_UI_CLI_DIR}")
  else()
    message(WARNING
      "EINHEIT_UI_WITH_ENGINE=ON but no CMakeLists.txt at "
      "${EINHEIT_UI_CLI_DIR}; confd adapter + command seam disabled")
  endif()
endif()

if(EINHEIT_UI_BUILD_ADAPTERS)
  add_subdirectory(adapters/example)
  add_subdirectory(adapters/hd_relay)
  add_subdirectory(adapters/takt)
  add_subdirectory(adapters/shell)
  add_subdirectory(adapters/editor)
  # The confd reference adapter drives the command engine, so it only
  # builds when the engine is linked.
  if(EINHEIT_UI_HAVE_ENGINE)
    add_subdirectory(adapters/confd)
  endif()
endif()

if(EINHEIT_UI_BUILD_BINARY)
  include("cmake/einheit_ui_binary.cmake")
  include("cmake/einheit_shell_launcher_binary.cmake")
endif()

if(EINHEIT_UI_BUILD_TESTS)
  enable_testing()
  include(CTest)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
  endif()
endif()

# Install the shared template tree.
install(
  DIRECTORY templates/
  DESTINATION share/einheit-ui/templates
  FILES_MATCHING PATTERN "*.inja" PATTERN "*.html" PATTERN "*.css"
)
install(
  DIRECTORY assets/
  DESTINATION share/einheit-ui/assets
  FILES_MATCHING
    PATTERN "*.js" PATTERN "*.css" PATTERN "*.svg" PATTERN "*.woff2"
)
