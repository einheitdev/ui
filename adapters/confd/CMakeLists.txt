add_library(einheit_ui_adapter_confd STATIC
  src/adapter.cc
)

target_include_directories(einheit_ui_adapter_confd
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(einheit_ui_adapter_confd
  PUBLIC
    einheit_ui
    einheit_ui_command
)

target_compile_definitions(einheit_ui_adapter_confd
  PRIVATE
    EINHEIT_UI_ADAPTER_CONFD_TEMPLATES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/templates"
)

install(
  DIRECTORY templates/
  DESTINATION share/einheit-ui/adapters/confd/templates
  FILES_MATCHING PATTERN "*.inja"
)
