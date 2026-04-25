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

message(STATUS "C compiler:   ${CMAKE_C_COMPILER}")
message(STATUS "C++ compiler: ${CMAKE_CXX_COMPILER}")

option(EINHEIT_UI_BUILD_TESTS    "Build the test suite"      ON)
option(EINHEIT_UI_BUILD_ADAPTERS "Build the product adapters" ON)
option(EINHEIT_UI_BUILD_BINARY   "Build the einheit-ui binary" ON)
option(EINHEIT_UI_TEMPLATE_HOT_RELOAD
  "Reload templates from disk on every render (debug only)" OFF)

# Where templates live at runtime. Default: alongside the binary; the
# adapter can override per-product.
set(EINHEIT_UI_TEMPLATES_DIR
  "${CMAKE_INSTALL_PREFIX}/share/einheit-ui/templates"
  CACHE PATH
  "Default runtime path for shared template files")

include("cmake/3rd_party_libs/fetch.cmake")
include("cmake/lib_ui_render.cmake")
include("cmake/lib_ui_core.cmake")

if(EINHEIT_UI_BUILD_ADAPTERS)
  add_subdirectory(adapters/example)
endif()

if(EINHEIT_UI_BUILD_BINARY)
  include("cmake/einheit_ui_binary.cmake")
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
