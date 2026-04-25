# Third-party dependencies for einheit-ui.
#
# Libraries:
#   Crow            (BSD-3)   — HTTP/SSE server
#   nlohmann_json   (MIT)     — JSON, also inja's data model
#   inja            (MIT)     — Jinja2-style HTML templating
#   spdlog          (MIT)     — logging
#   CLI11           (BSD-3)   — argument parsing for the binary
#   GoogleTest      (BSD-3)   — unit test framework
#
# System packages (find_package):
#   Threads, OpenSSL (Crow TLS), pkg-config

include(FetchContent)

# ----- Threads / OpenSSL ----------------------------------------------------
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)

# ----- nlohmann_json --------------------------------------------------------
find_package(nlohmann_json QUIET)
if(NOT TARGET nlohmann_json::nlohmann_json)
  FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
  )
  set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
  set(JSON_Install ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(nlohmann_json)
endif()

# ----- inja -----------------------------------------------------------------
FetchContent_Declare(inja
  GIT_REPOSITORY https://github.com/pantor/inja.git
  GIT_TAG v3.5.0
  GIT_SHALLOW TRUE
)
set(INJA_USE_EMBEDDED_JSON OFF CACHE BOOL "" FORCE)
set(INJA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(INJA_INSTALL ON CACHE BOOL "" FORCE)
set(BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(inja)

# ----- Crow -----------------------------------------------------------------
FetchContent_Declare(crow
  GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
  GIT_TAG v1.2.1.2
  GIT_SHALLOW TRUE
)
set(CROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CROW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CROW_ENABLE_SSL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(crow)

# ----- spdlog ---------------------------------------------------------------
set(SPDLOG_BUILD_SHARED ON CACHE BOOL "" FORCE)
set(SPDLOG_NO_EXCEPTIONS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.16.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(spdlog)

# ----- CLI11 ----------------------------------------------------------------
FetchContent_Declare(cli11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG v2.6.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(cli11)
if(TARGET CLI11 AND NOT TARGET CLI11::CLI11)
  add_library(CLI11::CLI11 ALIAS CLI11)
endif()

# ----- GoogleTest -----------------------------------------------------------
if(EINHEIT_UI_BUILD_TESTS)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE
  )
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()
