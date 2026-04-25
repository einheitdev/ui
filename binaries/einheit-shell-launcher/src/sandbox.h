/// @file sandbox.h
/// @brief Process-hardening primitives the einheit-shell-launcher
/// applies before exec'ing the cli. Each step is exposed as a free
/// function operating on plain data so it can be unit-tested
/// independently of the exec path. See main.cc for the order of
/// application.
// Copyright (c) 2026 Einheit Networks

#ifndef BINARIES_EINHEIT_SHELL_LAUNCHER_SRC_SANDBOX_H_
#define BINARIES_EINHEIT_SHELL_LAUNCHER_SRC_SANDBOX_H_

#include <expected>
#include <string>
#include <vector>

namespace einheit::ui::launcher::sandbox {

/// Errors raised by the hardening helpers. Each step returns a
/// distinct code so the launcher can log exactly which guard rail
/// failed before bailing out.
enum class SandboxError {
  RlimitFailed,
  EnvScrubFailed,
  ChdirFailed,
  DropPrivsFailed,
  NoNewPrivsFailed,
  SeccompFailed,
  ExecFailed,
};

/// Argv contract the launcher receives from einheit-ui. Plain data
/// so tests can build instances without going through CLI11.
struct Args {
  /// Absolute path to the einheit-cli binary to exec.
  std::string cli_path;
  /// Numeric uid to drop to. 0 means "do not drop" — useful for
  /// development on a machine without a dedicated einheit-shell
  /// account, but the production deploy MUST set this to a non-
  /// privileged uid.
  unsigned int uid = 0;
  /// Numeric gid to drop to. Same caveat as uid.
  unsigned int gid = 0;
  /// Caller role forwarded to einheit-cli via --role.
  std::string role;
  /// Caller user name forwarded to einheit-cli.
  std::string user;
  /// Adapter name (`example`, `hd-relay`, ...) forwarded as
  /// --adapter.
  std::string adapter;
  /// Optional --target value (named target from ~/.einheit/config).
  std::string target;
  /// Optional --endpoint override pointing at the daemon's local
  /// ipc:// socket.
  std::string endpoint;
  /// Optional --event-endpoint override.
  std::string event_endpoint;
  /// Working directory the launcher chdirs into before exec'ing
  /// the cli. Defaults to /tmp so the cli can't observe the
  /// caller's cwd.
  std::string workdir = "/tmp";
};

/// Apply resource limits the cli will inherit through execve.
/// Goal: cap blast radius if the cli (or anything it links) tries
/// to write, balloon memory, or open arbitrary fds.
///
/// - RLIMIT_FSIZE=16 MiB: caps any single regular file write so
///   a runaway log can't fill the disk; small working files
///   (example adapter schema dump, hd-relay catalog cache) fit
///   comfortably. ipc:// socket files are exempt from FSIZE.
/// - RLIMIT_NOFILE=64: more than enough for ZMQ + stdio + a
///   handful of inotify/eventfd descriptors, far less than the
///   default 1024.
/// - RLIMIT_AS=512 MiB: cli has no business going above ~50 MiB
///   in practice; this stops a runaway allocator.
/// - RLIMIT_CORE=0: no core dumps (could leak session tokens).
///
/// NPROC is applied separately (ApplyNprocLimit) after the uid
/// drop because the kernel checks RLIMIT_NPROC against the
/// calling uid's existing thread count — setting a low cap
/// before dropping out of the host uid would trip immediately.
auto ApplyRlimits()
    -> std::expected<void, SandboxError>;

/// Apply RLIMIT_NPROC=32. Called after DropPrivs so the cap
/// applies to the dedicated einheit-shell uid (which usually
/// has no other processes), not the host uid. No-op when uid
/// is 0 — in development mode we are still the host uid and
/// can't lower NPROC below the host's existing thread count.
auto ApplyNprocLimit(unsigned int uid)
    -> std::expected<void, SandboxError>;

/// Scrub the process environment to the minimal set the cli needs
/// to render a usable terminal session. Everything else
/// (PAGER/EDITOR/SHELL/HOME/...) is unset so a sandboxed user
/// can't influence pager/editor selection.
///
/// `term` and `lang` are passed in rather than read from the
/// inherited env so the launcher's caller (einheit-ui) controls
/// them — the caller already negotiated TERM with xterm.js.
///
/// @param term Value for TERM. Empty -> "xterm-256color".
/// @param lang Value for LANG/LC_ALL. Empty -> "C.UTF-8".
auto ScrubEnv(const std::string &term, const std::string &lang)
    -> std::expected<void, SandboxError>;

/// chdir into `path`. Done before drop_privs so the launcher can
/// reach the path even if the target uid lacks +x on the chain.
auto ChdirTo(const std::string &path)
    -> std::expected<void, SandboxError>;

/// Drop privileges to `gid` then `uid`, in that order — POSIX
/// requires gid to be set first so the credential check on
/// setuid still passes. Calls setgroups({}) to clear inherited
/// supplementary groups. No-op when uid == 0 (development mode).
auto DropPrivs(unsigned int uid, unsigned int gid)
    -> std::expected<void, SandboxError>;

/// Set PR_SET_NO_NEW_PRIVS=1 so a subsequent execve cannot pick
/// up new capabilities via setuid bits or file capabilities. The
/// kernel requires this before SECCOMP_SET_MODE_FILTER for any
/// caller that isn't CAP_SYS_ADMIN.
auto NoNewPrivs() -> std::expected<void, SandboxError>;

/// Install the seccomp-bpf filter that gates the syscalls the cli
/// is allowed to make. The default action is SECCOMP_RET_ERRNO
/// returning EPERM so a forbidden syscall surfaces as a normal
/// errno rather than an opaque SIGKILL — this makes
/// misconfigurations debuggable.
///
/// The filter is intentionally a generous allow-list: every
/// syscall ZMQ-over-ipc, libstdc++, and the cli's REPL need at
/// runtime is permitted. The denials are the ones identified in
/// the audit:
///   execve / execveat — no further binaries
///   ptrace            — no debug attach
///   mount / unmount2  — no fs reshape
///   chroot            — no namespace games
///   reboot, kexec_*   — no host control
///   init_module       — no kernel modules
///   prctl (most ops)  — gated to PR_SET_NAME, PR_GET_NAME
///   setuid / setgid   — privs already dropped
auto InstallSeccompFilter() -> std::expected<void, SandboxError>;

/// Build the argv the launcher will pass to execve. Always passes
/// `--locked` plus whichever optional flags the launcher received.
/// Exposed so tests can inspect the wire form independently of
/// the exec.
auto BuildCliArgv(const Args &args)
    -> std::vector<std::string>;

/// execve the cli with the argv from BuildCliArgv. Does not return
/// on success; on failure returns ExecFailed with errno mapped
/// into the message.
[[noreturn]] auto ExecCli(const Args &args, char **envp) -> void;

}  // namespace einheit::ui::launcher::sandbox

#endif  // BINARIES_EINHEIT_SHELL_LAUNCHER_SRC_SANDBOX_H_
