# einheit-shell-launcher — small hardening binary spawned per UI
# shell session. Drops privs + applies rlimits + installs a
# seccomp-bpf filter before exec'ing einheit-cli --locked.
#
# Linux-only. The seccomp/prctl headers it depends on are kernel
# headers shipped by glibc, not libseccomp; we keep the launcher's
# build-time dep set empty on purpose so it can be cross-built into
# a static binary without pulling in libseccomp-dev.

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(STATUS
    "einheit-shell-launcher skipped — Linux-only (uses seccomp-bpf)")
  return()
endif()

add_executable(einheit-shell-launcher
  binaries/einheit-shell-launcher/src/main.cc
  binaries/einheit-shell-launcher/src/sandbox.cc
)

target_include_directories(einheit-shell-launcher
  PRIVATE ${PROJECT_SOURCE_DIR})

target_link_libraries(einheit-shell-launcher
  PRIVATE CLI11::CLI11)

set_target_properties(einheit-shell-launcher PROPERTIES
  OUTPUT_NAME einheit-shell-launcher)

# Production deploys want this installed setuid-root so it can drop
# to einheit-shell on its own. The CMake install rule does not set
# the suid bit — the system packager (postinst script) is the right
# place for that, since it can also create the einheit-shell user.
install(TARGETS einheit-shell-launcher RUNTIME DESTINATION bin)
