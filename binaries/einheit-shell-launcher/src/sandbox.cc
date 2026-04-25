/// @file sandbox.cc
/// @brief Hardening primitives applied before exec'ing einheit-cli.
// Copyright (c) 2026 Einheit Networks

#include "binaries/einheit-shell-launcher/src/sandbox.h"

#include <grp.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <pwd.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <string>
#include <vector>

namespace einheit::ui::launcher::sandbox {
namespace {

auto SetOne(int resource, rlim_t soft, rlim_t hard)
    -> std::expected<void, SandboxError> {
  struct rlimit r {
    soft, hard
  };
  if (::setrlimit(resource, &r) != 0) {
    return std::unexpected(SandboxError::RlimitFailed);
  }
  return {};
}

}  // namespace

auto ApplyRlimits() -> std::expected<void, SandboxError> {
  // FSIZE caps the size of any single regular file the cli can
  // write. The cli legitimately writes small working files (the
  // example adapter dumps its schema YAML, hd-relay caches the
  // daemon's catalog; ZMQ ipc opens are *socket* files which
  // are exempt). 16 MiB is comfortably above any of those and
  // far below disk-fill territory.
  constexpr rlim_t kSixteenMib = 16ULL * 1024ULL * 1024ULL;
  if (auto r = SetOne(RLIMIT_FSIZE, kSixteenMib, kSixteenMib); !r) {
    return r;
  }
  // 64 fds is generous for ZMQ + stdio + a handful of eventfds.
  if (auto r = SetOne(RLIMIT_NOFILE, 64, 64); !r) return r;
  // 512 MiB cap on virtual memory. The cli sits well under 50 MiB
  // in practice; this catches a runaway allocator.
  constexpr rlim_t kHalfGib = 512ULL * 1024ULL * 1024ULL;
  if (auto r = SetOne(RLIMIT_AS, kHalfGib, kHalfGib); !r) return r;
  // Disable core dumps — they could leak the daemon's session
  // tokens out of memory.
  if (auto r = SetOne(RLIMIT_CORE, 0, 0); !r) return r;
  return {};
}

auto ApplyNprocLimit(unsigned int uid)
    -> std::expected<void, SandboxError> {
  // Development mode: launcher running as the host uid (e.g.
  // karl, with thousands of threads). Lowering NPROC under the
  // current count would trigger EAGAIN on the cli's first
  // pthread_create. In production --uid points at a dedicated
  // einheit-shell account that typically has zero other
  // processes; the cap then bites only on a fork-bomb attempt.
  if (uid == 0) return {};
  // 32 leaves headroom for ZMQ's io thread and a couple of
  // concurrent operator sessions sharing the einheit-shell uid.
  return SetOne(RLIMIT_NPROC, 32, 32);
}

auto ScrubEnv(const std::string &term, const std::string &lang)
    -> std::expected<void, SandboxError> {
  if (::clearenv() != 0) {
    return std::unexpected(SandboxError::EnvScrubFailed);
  }
  // Minimal PATH so getpwuid(3) and any libc internals that look
  // up programs find the basics. We do NOT keep the inherited
  // PATH because it could point at a writable directory the
  // sandboxed user controls.
  if (::setenv("PATH", "/usr/bin:/bin", 1) != 0) {
    return std::unexpected(SandboxError::EnvScrubFailed);
  }
  const std::string &t = term.empty() ? std::string("xterm-256color")
                                       : term;
  if (::setenv("TERM", t.c_str(), 1) != 0) {
    return std::unexpected(SandboxError::EnvScrubFailed);
  }
  const std::string &l = lang.empty() ? std::string("C.UTF-8") : lang;
  if (::setenv("LANG", l.c_str(), 1) != 0 ||
      ::setenv("LC_ALL", l.c_str(), 1) != 0) {
    return std::unexpected(SandboxError::EnvScrubFailed);
  }
  return {};
}

auto ChdirTo(const std::string &path)
    -> std::expected<void, SandboxError> {
  if (::chdir(path.c_str()) != 0) {
    return std::unexpected(SandboxError::ChdirFailed);
  }
  return {};
}

auto DropPrivs(unsigned int uid, unsigned int gid)
    -> std::expected<void, SandboxError> {
  // Development mode: launcher invoked as the same uid einheit-ui
  // already runs under. No drop possible; trust --locked plus
  // seccomp + rlimits. Production deploys MUST set uid != 0.
  if (uid == 0) return {};
  // Clear inherited supplementary groups before changing primary
  // gid so the resulting process has exactly {gid} and nothing
  // else.
  if (::setgroups(0, nullptr) != 0) {
    return std::unexpected(SandboxError::DropPrivsFailed);
  }
  if (::setresgid(static_cast<gid_t>(gid), static_cast<gid_t>(gid),
                  static_cast<gid_t>(gid)) != 0) {
    return std::unexpected(SandboxError::DropPrivsFailed);
  }
  if (::setresuid(static_cast<uid_t>(uid), static_cast<uid_t>(uid),
                  static_cast<uid_t>(uid)) != 0) {
    return std::unexpected(SandboxError::DropPrivsFailed);
  }
  // Belt and suspenders: setresuid succeeded but verify we cannot
  // regain root. If we can, abort — something is wrong with the
  // kernel or our privilege model.
  if (::setuid(0) == 0) {
    return std::unexpected(SandboxError::DropPrivsFailed);
  }
  return {};
}

auto NoNewPrivs() -> std::expected<void, SandboxError> {
  if (::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
    return std::unexpected(SandboxError::NoNewPrivsFailed);
  }
  return {};
}

namespace {

// Audit identified these as host-OS escape vectors. Each is
// denied with EPERM so the cli (or any future extension) hits a
// surfaceable errno rather than an opaque kill — debugging a
// surprise refusal is much easier than chasing SIGSYS.
//
// Order is: x86_64 syscall number followed by a brief comment on
// why it's denied. The kernel ABI is stable on x86_64 so the
// hard-coded numbers are safe; if einheit-ui ever ports to
// aarch64 the arch check at the top of the program kills the
// process and a port hand-fixes this list.
struct DeniedSyscall {
  int nr;
  const char *name;
};

// The launcher itself needs execve / execveat exactly once (to
// chain into einheit-cli) so we cannot deny them at this layer
// without blocking our own hand-off. einheit-cli's --locked mode
// installs a follow-on seccomp filter that denies both — seccomp
// filters are AND'd, so the post-exec cli ends up with no execve
// regardless of which layer thinks it's responsible.
constexpr DeniedSyscall kDenied[] = {
    {101, "ptrace"},         // No debug attach / mem peek.
    {165, "mount"},          // No fs reshape.
    {166, "umount2"},        // Same.
    {161, "chroot"},         // No namespace games.
    {155, "pivot_root"},     // Same.
    {169, "reboot"},         // No host control.
    {246, "kexec_load"},     // No kernel replacement.
    {320, "kexec_file_load"},
    {175, "init_module"},    // No kernel modules.
    {313, "finit_module"},
    {176, "delete_module"},
    {167, "swapon"},
    {168, "swapoff"},
    {133, "mknod"},          // No device file creation.
    {259, "mknodat"},
    {105, "setuid"},         // Privs already dropped.
    {106, "setgid"},
    {113, "setreuid"},
    {114, "setregid"},
    {117, "setresuid"},
    {119, "setresgid"},
    {122, "setfsuid"},
    {123, "setfsgid"},
    {126, "capset"},         // No capability gain.
    {321, "bpf"},            // No eBPF programs.
    {298, "perf_event_open"},  // No perf access.
    {323, "userfaultfd"},    // No fault tricks.
    {425, "io_uring_setup"}, // No io_uring (large surface).
    {426, "io_uring_enter"},
    {427, "io_uring_register"},
    // clone3 stays allowed — glibc's pthread_create uses it on
    // modern kernels and ZMQ's iothread depends on
    // pthread_create. We can't filter its flags via BPF (the
    // struct is passed by pointer) so namespace-creation
    // protection moves to unshare/setns below, both of which
    // are denied.
    {272, "unshare"},        // No new namespaces.
    {308, "setns"},
    {103, "syslog"},         // No dmesg read.
    {250, "keyctl"},         // No kernel keyring.
    {248, "request_key"},
    {249, "add_key"},
    {172, "iopl"},           // No io ports.
    {173, "ioperm"},
};

}  // namespace

auto InstallSeccompFilter() -> std::expected<void, SandboxError> {
  // BPF program: validate arch, then for each denied syscall
  // return EPERM, otherwise allow. Default-allow keeps the cli
  // working without us tracking every libc syscall it touches.
  std::vector<sock_filter> prog;

  // Load arch from seccomp_data and reject anything not x86_64.
  // The launcher is built x86_64-only today; if the cli is ever
  // re-targeted to aarch64 this kill_process forces an explicit
  // port rather than silently letting an attacker hop via 32-bit
  // shims.
  prog.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                          offsetof(struct seccomp_data, arch)));
  prog.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K,
                          AUDIT_ARCH_X86_64, 1, 0));
  prog.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS));

  // Load syscall number.
  prog.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
                          offsetof(struct seccomp_data, nr)));

  // For each denied syscall: BPF_JEQ jumps over the EPERM-return
  // when the nr does NOT match. When it does match, fall through
  // and return EPERM. (jt=0 -> next insn is RET; jf=1 -> skip
  // RET to the next JEQ.)
  constexpr std::uint32_t kPermErrno =
      SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA);
  for (const auto &d : kDenied) {
    const auto nr = static_cast<std::uint32_t>(d.nr);
    prog.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, nr, 0, 1));
    prog.push_back(BPF_STMT(BPF_RET | BPF_K, kPermErrno));
  }

  // Default: allow.
  prog.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW));

  struct sock_fprog fprog {};
  fprog.len = static_cast<unsigned short>(prog.size());
  fprog.filter = prog.data();
  // SYS_seccomp is portable; libseccomp is not pulled in to keep
  // the launcher's build-time dep set empty.
  if (::syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0u,
                &fprog) != 0) {
    return std::unexpected(SandboxError::SeccompFailed);
  }
  return {};
}

auto BuildCliArgv(const Args &args) -> std::vector<std::string> {
  std::vector<std::string> v;
  v.push_back(args.cli_path);
  v.push_back("--locked");
  if (!args.adapter.empty()) {
    v.push_back("--adapter");
    v.push_back(args.adapter);
  }
  if (!args.role.empty()) {
    v.push_back("--role");
    v.push_back(args.role);
  }
  if (!args.target.empty()) {
    v.push_back("--target");
    v.push_back(args.target);
  }
  if (!args.endpoint.empty()) {
    v.push_back("--endpoint");
    v.push_back(args.endpoint);
  }
  if (!args.event_endpoint.empty()) {
    v.push_back("--event-endpoint");
    v.push_back(args.event_endpoint);
  }
  return v;
}

[[noreturn]] auto ExecCli(const Args &args, char **envp) -> void {
  const auto argv_strings = BuildCliArgv(args);
  std::vector<char *> argv;
  argv.reserve(argv_strings.size() + 1);
  for (const auto &s : argv_strings) {
    argv.push_back(const_cast<char *>(s.c_str()));
  }
  argv.push_back(nullptr);
  ::execve(args.cli_path.c_str(), argv.data(), envp);
  // If execve returns, it failed. Surface errno on stderr (the
  // PTY) so einheit-ui can see what broke, then exit non-zero.
  const auto err = errno;
  ::dprintf(STDERR_FILENO,
            "einheit-shell-launcher: execve(%s) failed: %s\n",
            args.cli_path.c_str(), ::strerror(err));
  std::_Exit(127);
}

}  // namespace einheit::ui::launcher::sandbox
