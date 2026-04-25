add_library(einheit_ui_adapter_hd_relay STATIC
  src/adapter.cc
  src/hd_client.cc
)

target_include_directories(einheit_ui_adapter_hd_relay
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(einheit_ui_adapter_hd_relay
  PUBLIC
    einheit_ui
    httplib::httplib
)

target_compile_definitions(einheit_ui_adapter_hd_relay
  PRIVATE
    EINHEIT_UI_ADAPTER_HD_RELAY_TEMPLATES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/templates"
)

install(
  DIRECTORY templates/
  DESTINATION share/einheit-ui/adapters/hd-relay/templates
  FILES_MATCHING PATTERN "*.inja"
)
