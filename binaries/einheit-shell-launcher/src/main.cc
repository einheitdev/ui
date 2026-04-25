/// @file main.cc
/// @brief einheit-shell-launcher — hardens the process before
/// exec'ing einheit-cli --locked. Spawned per UI shell session by
/// the einheit-ui shell adapter (PTY child).
///
/// Order of operations (each gated by std::expected):
///   1. Parse argv.
///   2. ApplyRlimits — caps fork, fd, fs writes, vmem.
///   3. ScrubEnv — drop everything except a minimal PATH/TERM/LANG.
///   4. ChdirTo — leave the caller's cwd.
///   5. DropPrivs — setresgid + setresuid + setgroups({}).
///   6. NoNewPrivs — required before SECCOMP_SET_MODE_FILTER.
///   7. InstallSeccompFilter — host-OS escape vectors -> EPERM.
///   8. ExecCli — execve einheit --locked + forwarded flags.
// Copyright (c) 2026 Einheit Networks

#include <cstdlib>
#include <iostream>
#include <string>

#include <CLI/CLI.hpp>

#include "binaries/einheit-shell-launcher/src/sandbox.h"

namespace {

auto Fail(const char *step,
          einheit::ui::launcher::sandbox::SandboxError) -> int {
  // Do not include errno text or syscall details in the message.
  // The launcher writes to the operator's PTY; leaking sandbox
  // internals to a partly-trusted user gives them a roadmap.
  std::cerr << "einheit-shell-launcher: " << step
            << " failed — refusing to exec.\n";
  return 1;
}

}  // namespace

extern "C" char **environ;

auto main(int argc, char **argv) -> int {
  using namespace einheit::ui::launcher::sandbox;

  Args a;
  CLI::App app{
      "einheit-shell-launcher — drop privs and exec einheit --locked"};
  app.add_option("--cli", a.cli_path,
                 "Absolute path to the einheit binary")
      ->required();
  app.add_option("--uid", a.uid,
                 "Numeric uid to drop to (0 = no drop, dev only)");
  app.add_option("--gid", a.gid,
                 "Numeric gid to drop to (0 = no drop, dev only)");
  app.add_option("--user", a.user,
                 "Operator name forwarded to the cli via "
                 "EINHEIT_USER (preserves audit identity past the "
                 "uid drop)");
  app.add_option("--role", a.role,
                 "Operator role: admin | operator | any");
  app.add_option("--adapter", a.adapter,
                 "Cli adapter: example | hd-relay");
  app.add_option("--target", a.target,
                 "Optional named target from ~/.einheit/config");
  app.add_option("--endpoint", a.endpoint,
                 "Optional control-socket override (ipc:// or tcp://)");
  app.add_option("--event-endpoint", a.event_endpoint,
                 "Optional event-socket override");
  app.add_option("--workdir", a.workdir,
                 "Directory to chdir into before exec (default /tmp)");
  app.add_flag("--learn", a.learn,
               "Pass --learn to the cli (in-process learning "
               "daemon — operator can drive configure/commit "
               "without a real product daemon).");
  std::string term;
  std::string lang;
  app.add_option("--term", term,
                 "TERM passed to the cli (default xterm-256color)");
  app.add_option("--lang", lang,
                 "LANG / LC_ALL passed to the cli (default C.UTF-8)");

  try {
    app.parse(argc, argv);
  } catch (const CLI::ParseError &e) {
    return app.exit(e);
  }

  // 1. Resource limits. Done before drop_privs because some
  //    setrlimit raises (e.g. NOFILE upward) need CAP_SYS_RESOURCE
  //    which we will not have post-drop. We are only lowering
  //    here, so it would work either way, but order matters when
  //    the launcher is later asked to raise a soft limit.
  if (auto r = ApplyRlimits(); !r) return Fail("rlimit", r.error());

  // 2. Scrub env to a known minimal set. We capture the user
  //    name before clearenv into EINHEIT_USER + EINHEIT_ROLE so
  //    auth::ResolveLocal in the cli sees the operator identity
  //    through the env after we drop privs.
  if (auto r = ScrubEnv(term, lang); !r) {
    return Fail("env scrub", r.error());
  }
  if (!a.user.empty()) {
    ::setenv("EINHEIT_USER", a.user.c_str(), 1);
  }
  if (!a.role.empty()) {
    ::setenv("EINHEIT_ROLE", a.role.c_str(), 1);
  }

  // 3. chdir before drop_privs so we can reach /tmp / our chosen
  //    workdir even if einheit-shell lacks +x on a parent dir.
  if (auto r = ChdirTo(a.workdir); !r) {
    return Fail("chdir", r.error());
  }

  // 4. Drop privileges. setresgid + setresuid + setgroups({}).
  //    No-op when uid==0 (development). Production deploys MUST
  //    set --uid to a dedicated einheit-shell account.
  if (auto r = DropPrivs(a.uid, a.gid); !r) {
    return Fail("drop privs", r.error());
  }

  // 4b. Now that the host uid is no longer this process's owner,
  //     we can safely cap RLIMIT_NPROC against the dedicated
  //     uid's (typically empty) thread count.
  if (auto r = ApplyNprocLimit(a.uid); !r) {
    return Fail("nproc", r.error());
  }

  // 5. PR_SET_NO_NEW_PRIVS is required by the kernel before any
  //    non-CAP_SYS_ADMIN process can install a seccomp filter,
  //    AND it ensures the imminent execve cannot pick up new
  //    capabilities via setuid bits.
  if (auto r = NoNewPrivs(); !r) {
    return Fail("no_new_privs", r.error());
  }

  // 6. Install the seccomp-bpf filter. Default-allow with the
  //    audit's host-OS escape vectors denied (EPERM, not SIGSYS,
  //    so failures are debuggable).
  if (auto r = InstallSeccompFilter(); !r) {
    return Fail("seccomp", r.error());
  }

  // 7. exec the cli. Does not return.
  ExecCli(a, environ);
}
