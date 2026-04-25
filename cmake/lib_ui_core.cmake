# libui_core — route helpers, SSE event stream, adapter contract.
# This is the headline target adapters and binaries link against.

add_library(ui_core_obj OBJECT
  src/route.cc
  src/stream.cc
  src/server.cc
  src/static_files.cc
)

target_include_directories(ui_core_obj
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(ui_core_obj
  PUBLIC
    ui_render
    Crow::Crow
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

add_library(einheit_ui STATIC
  $<TARGET_OBJECTS:ui_core_obj>
)

target_include_directories(einheit_ui
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(einheit_ui
  PUBLIC
    ui_render
    Crow::Crow
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)
