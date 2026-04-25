add_library(einheit_ui_adapter_shell STATIC
  src/adapter.cc
  src/pty_session.cc
)

target_include_directories(einheit_ui_adapter_shell
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(einheit_ui_adapter_shell
  PUBLIC
    einheit_ui
)

target_compile_definitions(einheit_ui_adapter_shell
  PRIVATE
    EINHEIT_UI_ADAPTER_SHELL_TEMPLATES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/templates"
)

# pty.h on Linux lives in libutil (forkpty, openpty); link it.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(einheit_ui_adapter_shell PUBLIC util)
endif()

install(
  DIRECTORY templates/
  DESTINATION share/einheit-ui/adapters/shell/templates
  FILES_MATCHING PATTERN "*.inja"
)
