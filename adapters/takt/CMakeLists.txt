add_library(einheit_ui_adapter_takt STATIC
  src/adapter.cc
  src/takt_client.cc
)

target_include_directories(einheit_ui_adapter_takt
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(einheit_ui_adapter_takt
  PUBLIC
    einheit_ui
    httplib::httplib
)

target_compile_definitions(einheit_ui_adapter_takt
  PRIVATE
    EINHEIT_UI_ADAPTER_TAKT_TEMPLATES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/templates"
)

install(
  DIRECTORY templates/
  DESTINATION share/einheit-ui/adapters/takt/templates
  FILES_MATCHING PATTERN "*.inja"
)
