# libui_render — template engine + theme + html helpers.

add_library(ui_render_obj OBJECT
  src/render/template_engine.cc
  src/render/theme.cc
  src/render/escape.cc
)

target_include_directories(ui_render_obj
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(ui_render_obj
  PUBLIC
    pantor::inja
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

if(EINHEIT_UI_TEMPLATE_HOT_RELOAD)
  target_compile_definitions(ui_render_obj
    PUBLIC EINHEIT_UI_TEMPLATE_HOT_RELOAD=1)
endif()

target_compile_definitions(ui_render_obj
  PUBLIC
    EINHEIT_UI_TEMPLATES_DIR="${EINHEIT_UI_TEMPLATES_DIR}"
)

add_library(ui_render STATIC
  $<TARGET_OBJECTS:ui_render_obj>
)

target_include_directories(ui_render
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(ui_render
  PUBLIC
    pantor::inja
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)
