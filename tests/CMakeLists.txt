include(GoogleTest)

function(einheit_ui_add_test name)
  add_executable(${name} ${name}.cc)
  target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR})
  target_link_libraries(${name}
    PRIVATE
      einheit_ui
      GTest::gtest
      GTest::gtest_main
  )
  gtest_discover_tests(${name}
    DISCOVERY_TIMEOUT 10
    PROPERTIES TIMEOUT 30
  )
endfunction()

einheit_ui_add_test(test_escape)
einheit_ui_add_test(test_theme)
einheit_ui_add_test(test_template_engine)
einheit_ui_add_test(test_route)
einheit_ui_add_test(test_stream)

# test_partials renders framework templates from disk; needs the
# templates dir baked in.
add_executable(test_partials test_partials.cc)
target_include_directories(test_partials PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_definitions(test_partials
  PRIVATE
    EINHEIT_UI_FRAMEWORK_TEMPLATES_DIR="${PROJECT_SOURCE_DIR}/templates"
)
target_link_libraries(test_partials
  PRIVATE
    einheit_ui
    GTest::gtest
    GTest::gtest_main
)
gtest_discover_tests(test_partials
  DISCOVERY_TIMEOUT 10
  PROPERTIES TIMEOUT 30
)

# hd-relay adapter contract test links against the hd-relay
# adapter library; renders its templates against fake JSON shaped
# like hd's actual responses.
add_executable(test_hd_relay_adapter test_hd_relay_adapter.cc)
target_include_directories(test_hd_relay_adapter
  PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_definitions(test_hd_relay_adapter
  PRIVATE
    EINHEIT_UI_FRAMEWORK_TEMPLATES_DIR="${PROJECT_SOURCE_DIR}/templates"
)
target_link_libraries(test_hd_relay_adapter
  PRIVATE
    einheit_ui
    einheit_ui_adapter_hd_relay
    GTest::gtest
    GTest::gtest_main
)
gtest_discover_tests(test_hd_relay_adapter
  DISCOVERY_TIMEOUT 10
  PROPERTIES TIMEOUT 30
)

# Adapter contract test links the shipped example adapter to assert
# the templates render and the route registrations behave.
add_executable(test_example_adapter test_example_adapter.cc)
target_include_directories(test_example_adapter
  PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_definitions(test_example_adapter
  PRIVATE
    EINHEIT_UI_FRAMEWORK_TEMPLATES_DIR="${PROJECT_SOURCE_DIR}/templates"
)
target_link_libraries(test_example_adapter
  PRIVATE
    einheit_ui
    einheit_ui_adapter_example
    GTest::gtest
    GTest::gtest_main
)
gtest_discover_tests(test_example_adapter
  DISCOVERY_TIMEOUT 10
  PROPERTIES TIMEOUT 30
)

# Shell adapter mount-time validation. The PTY/WS bridge isn't
# exercised here — that requires real launcher/cli binaries.
add_executable(test_shell_adapter test_shell_adapter.cc)
target_include_directories(test_shell_adapter
  PRIVATE ${PROJECT_SOURCE_DIR})
target_compile_definitions(test_shell_adapter
  PRIVATE
    EINHEIT_UI_FRAMEWORK_TEMPLATES_DIR="${PROJECT_SOURCE_DIR}/templates"
)
target_link_libraries(test_shell_adapter
  PRIVATE
    einheit_ui
    einheit_ui_adapter_shell
    GTest::gtest
    GTest::gtest_main
)
gtest_discover_tests(test_shell_adapter
  DISCOVERY_TIMEOUT 10
  PROPERTIES TIMEOUT 30
)

# einheit-shell-launcher argv-shape test. Links the launcher's
# sandbox object directly so we can call BuildCliArgv without
# building or executing the launcher binary itself.
if(TARGET einheit-shell-launcher)
  add_executable(test_shell_launcher_argv
    ${PROJECT_SOURCE_DIR}/binaries/einheit-shell-launcher/tests/test_argv.cc
    ${PROJECT_SOURCE_DIR}/binaries/einheit-shell-launcher/src/sandbox.cc
  )
  target_include_directories(test_shell_launcher_argv
    PRIVATE ${PROJECT_SOURCE_DIR})
  target_link_libraries(test_shell_launcher_argv
    PRIVATE GTest::gtest GTest::gtest_main)
  gtest_discover_tests(test_shell_launcher_argv
    DISCOVERY_TIMEOUT 10
    PROPERTIES TIMEOUT 30
  )
endif()
