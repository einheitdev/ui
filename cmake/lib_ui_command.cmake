# libui_command — the UI's command-driver seam over the shared cli
# command engine. Deliberately isolated from the Crow-dependent
# ui_core: this target links `einheit_cli` (which drags in zmq /
# sodium / replxx) but NOT Crow, so the engine seam can be unit-tested
# on its own and adapters that don't drive mutations don't pay for it.

add_library(ui_command_obj OBJECT
  src/command_driver.cc
)

target_include_directories(ui_command_obj
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(ui_command_obj
  PUBLIC
    einheit_cli
)

add_library(einheit_ui_command STATIC
  $<TARGET_OBJECTS:ui_command_obj>
)

target_include_directories(einheit_ui_command
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(einheit_ui_command
  PUBLIC
    einheit_cli
)
