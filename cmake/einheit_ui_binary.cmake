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
    CLI11::CLI11
)

set_target_properties(einheit-ui PROPERTIES
  OUTPUT_NAME einheit-ui
)

install(TARGETS einheit-ui RUNTIME DESTINATION bin)
