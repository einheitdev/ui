/// @file command_driver.h
/// @brief The UI's command-driver seam — the UI's equivalent of the
/// shell's `Dispatch`.
///
/// This is the heart of "UI drives the CLI command engine". Every
/// *mutating* UI action turns into a call to CommandDriver::Drive,
/// which builds a ParsedCommand and hands it to `cli::engine::Execute`
/// — the same single chokepoint the interactive shell uses. Role
/// gating and audit logging therefore happen once, in the engine, for
/// both front-ends: a mutation from a button produces an audit record
/// equivalent to the same command typed in the CLI, and a role the
/// operator lacks is rejected identically.
///
/// The driver is deliberately Crow-free so the seam can be unit-tested
/// without an HTTP server: an adapter route extracts the caller
/// identity from the request (see MakeCaller) and calls Drive; the
/// countdown / running-config *reads* stay on the fast direct path and
/// do NOT come through here (that is what keeps the audit log a log of
/// mutations, not of every counter poll).
// Copyright (c) 2026 Einheit Networks

#ifndef INCLUDE_EINHEIT_UI_COMMAND_DRIVER_H_
#define INCLUDE_EINHEIT_UI_COMMAND_DRIVER_H_

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "einheit/cli/audit.h"
#include "einheit/cli/auth.h"
#include "einheit/cli/command_tree.h"
#include "einheit/cli/engine.h"
#include "einheit/cli/session.h"
#include "einheit/cli/transport/transport.h"

namespace einheit::ui {

/// How a UI-driven command resolved. Flat enough that a route handler
/// can map it straight onto an HTTP status without inspecting the
/// engine's error category.
enum class DriveStatus {
  /// Wire round-trip succeeded and the daemon returned Ok.
  Ok,
  /// Wire round-trip succeeded but the daemon returned an Error.
  DaemonError,
  /// Engine refused: caller's role does not satisfy the command gate.
  RoleForbidden,
  /// Engine refused: command needs a `configure` session and none is
  /// open.
  SessionRequired,
  /// Engine refused: no transport is attached.
  TransportUnavailable,
  /// Engine refused: the command is framework-local, not a wire verb.
  NotAWireCommand,
  /// Wire round-trip timed out.
  WireTimeout,
  /// Wire round-trip failed for a non-timeout reason.
  WireFailed,
  /// The driver has no spec registered for the requested path.
  UnknownCommand,
};

/// Structured result of driving one command from the UI. Carries
/// everything a route handler needs to render a response and nothing
/// terminal-specific.
struct DriveResult {
  /// Coarse resolution used for HTTP status mapping.
  DriveStatus status = DriveStatus::UnknownCommand;
  /// True iff the command succeeded end to end (wire Ok + daemon Ok).
  bool ok = false;
  /// Canonical command path that was driven (e.g. "commit confirmed").
  std::string command;
  /// Machine-readable outcome note ("ok", a daemon error code, or a
  /// rejection reason). Mirrors the audit record's `outcome`.
  std::string outcome;
  /// Human-readable message suitable for a toast or error panel.
  std::string message;
  /// Decoded string payload from the daemon response (confd encodes
  /// its response bodies as raw UTF-8). Empty when there is none.
  std::string data;
  /// Candidate-session state after the command (drives the UI's
  /// "pending candidate" affordance).
  bool in_configure = false;
  /// Measured wire round-trip time.
  std::chrono::milliseconds rtt{0};
};

/// Map a role string ("admin" / "operator" / "readonly" / …) to the
/// RoleGate the caller holds. Unknown / empty strings resolve to the
/// least-privileged AnyAuthenticated, so a mis-configured identity can
/// never accidentally gain operator rights.
/// @param role Case-insensitive role name.
/// @returns The RoleGate the caller holds.
auto ParseRole(std::string_view role) -> cli::RoleGate;

/// Build a CallerIdentity for a UI request. The UI is a front-end over
/// the engine exactly like the shell, so it stamps the acting operator
/// the same way — user name + resolved role — which is what makes the
/// audit record and role gate attribute correctly.
/// @param user Operator user name (audit identity).
/// @param role Role string; parsed via ParseRole.
/// @param source_addr Optional client address for the audit trail.
/// @returns A populated CallerIdentity with transport "ui".
auto MakeCaller(std::string user, std::string_view role,
                std::string source_addr = "")
    -> cli::auth::CallerIdentity;

/// The confd config-lifecycle command set, ready to drive. These are
/// the framework confd verbs (candidate / commit / rollback / confirm)
/// with their wire commands, role gates, and session requirements set
/// to match what `confd::Runtime` enforces — so any confd-backed
/// product UI reuses one definition instead of re-deriving it.
/// @returns A CommandTree registered with the confd verbs.
auto BuildConfdCommandTree() -> cli::CommandTree;

/// Drives UI mutations through the shared command engine.
///
/// One driver instance owns one candidate-config Session, guarded by a
/// mutex: HTTP requests arrive on many Crow worker threads, but the
/// candidate is a single shared editing context (confd is single-
/// editor with edit-locking), so serialising Drive calls is both
/// correct and the intended "two editors can't clobber each other"
/// behaviour. The transport and audit sink are borrowed and must
/// outlive the driver.
class CommandDriver {
 public:
  /// @param tree Command set this driver can invoke (copied).
  /// @param tx Connected transport to the engine/daemon (borrowed).
  /// @param audit Sink the engine writes one Record to per mutation.
  /// @param timeout Per-request wire timeout.
  CommandDriver(cli::CommandTree tree, cli::transport::Transport *tx,
                cli::audit::Sink audit,
                std::chrono::milliseconds timeout =
                    std::chrono::seconds(30));

  CommandDriver(const CommandDriver &) = delete;
  auto operator=(const CommandDriver &) -> CommandDriver & = delete;

  /// Drive one command as `caller`. Looks up the spec, builds a
  /// ParsedCommand, and executes it through `cli::engine::Execute`
  /// against this driver's session. Never throws and never returns an
  /// error out-of-band: a rejection is a DriveResult with `ok=false`
  /// and a status the caller can render. The engine emits the audit
  /// record (including on rejection), so the audit trail is complete
  /// regardless of the returned status.
  /// @param caller Acting operator identity + role.
  /// @param path Canonical command path (e.g. "set", "commit
  ///   confirmed").
  /// @param args Positional args in spec order.
  /// @param flags Named flags (no leading dashes).
  /// @returns Structured result for the route handler.
  auto Drive(const cli::auth::CallerIdentity &caller,
             const std::string &path,
             std::vector<std::string> args = {},
             std::unordered_map<std::string, std::string> flags = {})
      -> DriveResult;

  /// Whether a candidate-config session is currently open (the UI
  /// shows a "pending candidate" banner while true). Thread-safe.
  auto InConfigure() const -> bool;

  /// The active candidate-session id, if any. Thread-safe.
  auto SessionId() const -> std::optional<std::string>;

 private:
  cli::CommandTree tree_;
  cli::transport::Transport *tx_;
  cli::audit::Sink audit_;
  std::chrono::milliseconds timeout_;
  mutable std::mutex mu_;
  cli::Session session_;
};

}  // namespace einheit::ui

#endif  // INCLUDE_EINHEIT_UI_COMMAND_DRIVER_H_
