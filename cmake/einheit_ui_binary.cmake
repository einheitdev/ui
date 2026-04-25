# Top-level `einheit-ui` server binary. Mounts the framework + an
# adapter and listens on HTTP(S).

add_executable(einheit-ui
  binaries/einheit-ui/src/main.cc
)

target_link_libraries(einheit-ui
  PRIVATE
    einheit_ui
    einheit_ui_adapter_example
    einheit_ui_adapter_hd_relay
    einheit_ui_adapter_shell
    einheit_ui_adapter_editor
    CLI11::CLI11
)

# Dev-mode fallbacks. The binary checks the install path first
# (EINHEIT_UI_TEMPLATES_DIR via the libui_render objlib) and falls
# back to these source-tree paths so `./build/einheit-ui` works
# in-tree without `make install`.
target_compile_definitions(einheit-ui
  PRIVATE
    EINHEIT_UI_DEV_TEMPLATES_DIR="${PROJECT_SOURCE_DIR}/templates"
    EINHEIT_UI_DEV_ASSETS_DIR="${PROJECT_SOURCE_DIR}/assets"
    EINHEIT_UI_INSTALLED_ASSETS_DIR="${CMAKE_INSTALL_PREFIX}/share/einheit-ui/assets"
)

set_target_properties(einheit-ui PROPERTIES
  OUTPUT_NAME einheit-ui
)

install(TARGETS einheit-ui RUNTIME DESTINATION bin)
