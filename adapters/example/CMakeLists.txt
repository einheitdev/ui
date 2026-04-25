add_library(einheit_ui_adapter_example STATIC
  src/adapter.cc
)

target_include_directories(einheit_ui_adapter_example
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(einheit_ui_adapter_example
  PUBLIC
    einheit_ui
)

target_compile_definitions(einheit_ui_adapter_example
  PRIVATE
    EINHEIT_UI_ADAPTER_EXAMPLE_TEMPLATES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/templates"
)

install(
  DIRECTORY templates/
  DESTINATION share/einheit-ui/adapters/example/templates
  FILES_MATCHING PATTERN "*.inja"
)
