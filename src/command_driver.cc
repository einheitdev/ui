/// @file command_driver.cc
/// @brief CommandDriver implementation — the UI's front-end over the
/// shared command engine.
// Copyright (c) 2026 Einheit Networks

#include "einheit/ui/command_driver.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <string>
#include <utility>

namespace einheit::ui {
namespace {

/// Lower-case a short ASCII token (role names). Avoids a locale
/// dependency on the audit path.
auto Lower(std::string_view s) -> std::string {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

/// Register one confd verb, ignoring the (impossible on a fresh tree)
/// duplicate error so the builder reads as a flat list.
auto Add(cli::CommandTree &tree, cli::CommandSpec spec) -> void {
  (void)cli::Register(tree, std::move(spec));
}

}  // namespace

auto ParseRole(std::string_view role) -> cli::RoleGate {
  const auto r = Lower(role);
  if (r == "admin" || r == "administrator") return cli::RoleGate::AdminOnly;
  if (r == "operator" || r == "op") return cli::RoleGate::OperatorOrAdmin;
  // readonly / viewer / unknown / empty all map to the least-privileged
  // gate: an unrecognised identity never gains write rights by default.
  return cli::RoleGate::AnyAuthenticated;
}

auto MakeCaller(std::string user, std::string_view role,
                std::string source_addr) -> cli::auth::CallerIdentity {
  cli::auth::CallerIdentity id;
  id.user = std::move(user);
  id.role = ParseRole(role);
  id.transport = "ui";
  id.source_addr = std::move(source_addr);
  return id;
}

auto BuildConfdCommandTree() -> cli::CommandTree {
  using cli::ArgSpec;
  using cli::CommandSpec;
  using cli::RoleGate;
  cli::CommandTree tree;

  // Opening a candidate session is itself a mutating, audited action.
  Add(tree, CommandSpec{.path = "configure",
                        .role = RoleGate::OperatorOrAdmin,
                        .wire_command = "configure",
                        .help = "Open a candidate-config session."});
  Add(tree, CommandSpec{
                .path = "set",
                .args = {ArgSpec{.name = "path", .help = "config path"},
                         ArgSpec{.name = "value", .help = "value"}},
                .role = RoleGate::OperatorOrAdmin,
                .wire_command = "set",
                .help = "Set a value in the candidate.",
                .requires_session = true});
  Add(tree, CommandSpec{
                .path = "delete",
                .args = {ArgSpec{.name = "path", .help = "config path"}},
                .role = RoleGate::OperatorOrAdmin,
                .wire_command = "delete",
                .help = "Delete a value from the candidate.",
                .requires_session = true});
  Add(tree, CommandSpec{.path = "commit",
                        .role = RoleGate::OperatorOrAdmin,
                        .wire_command = "commit",
                        .help = "Commit the candidate.",
                        .requires_session = true});
  Add(tree, CommandSpec{
                .path = "commit confirmed",
                .args = {ArgSpec{.name = "minutes", .help = "revert window"}},
                .role = RoleGate::OperatorOrAdmin,
                .wire_command = "commit_confirmed",
                .help = "Commit with an auto-revert window.",
                .requires_session = true});
  Add(tree, CommandSpec{.path = "confirm",
                        .role = RoleGate::OperatorOrAdmin,
                        .wire_command = "confirm",
                        .help = "Confirm a pending commit-confirmed."});
  Add(tree, CommandSpec{.path = "rollback previous",
                        .role = RoleGate::OperatorOrAdmin,
                        .wire_command = "rollback_previous",
                        .help = "Roll back to the previous commit."});
  Add(tree, CommandSpec{
                .path = "rollback to",
                .args = {ArgSpec{.name = "rev", .help = "commit id"}},
                .role = RoleGate::OperatorOrAdmin,
                .wire_command = "rollback_to",
                .help = "Roll back to a specific commit."});
  return tree;
}

CommandDriver::CommandDriver(cli::CommandTree tree,
                             cli::transport::Transport *tx,
                             cli::audit::Sink audit,
                             std::chrono::milliseconds timeout)
    : tree_(std::move(tree)),
      tx_(tx),
      audit_(std::move(audit)),
      timeout_(timeout) {}

auto CommandDriver::Drive(
    const cli::auth::CallerIdentity &caller, const std::string &path,
    std::vector<std::string> args,
    std::unordered_map<std::string, std::string> flags) -> DriveResult {
  DriveResult out;
  out.command = path;

  std::lock_guard<std::mutex> lk(mu_);
  const auto it = tree_.by_path.find(path);
  if (it == tree_.by_path.end()) {
    out.status = DriveStatus::UnknownCommand;
    out.outcome = "unknown command";
    out.message = std::format("no such command: {}", path);
    out.in_configure = session_.in_configure;
    return out;
  }

  cli::ParsedCommand parsed;
  // Pointer into tree_.by_path: stable for the driver's lifetime (the
  // tree is not mutated after construction).
  parsed.spec = &it->second;
  parsed.args = std::move(args);
  parsed.flags = std::move(flags);

  cli::engine::Context ctx;
  ctx.tx = tx_;
  ctx.session = &session_;
  ctx.caller = caller;
  ctx.audit = audit_;
  ctx.timeout = timeout_;

  auto res = cli::engine::Execute(ctx, parsed);
  out.in_configure = session_.in_configure;

  if (!res) {
    // Pre-wire engine rejection. The engine has already emitted the
    // audit record; we just translate the category for the UI.
    out.message = res.error().message;
    switch (res.error().code) {
      case cli::engine::EngineError::RoleForbidden:
        out.status = DriveStatus::RoleForbidden;
        out.outcome = "role forbidden";
        break;
      case cli::engine::EngineError::SessionRequired:
        out.status = DriveStatus::SessionRequired;
        out.outcome = "session required";
        break;
      case cli::engine::EngineError::TransportUnavailable:
        out.status = DriveStatus::TransportUnavailable;
        out.outcome = "no transport";
        break;
      case cli::engine::EngineError::NotAWireCommand:
        out.status = DriveStatus::NotAWireCommand;
        out.outcome = "not a wire command";
        break;
    }
    return out;
  }

  const auto &oc = *res;
  out.rtt = oc.rtt;
  if (oc.wire == cli::engine::WireStatus::Timeout) {
    out.status = DriveStatus::WireTimeout;
    out.outcome = "timeout";
    out.message = oc.error_message;
    return out;
  }
  if (oc.wire == cli::engine::WireStatus::Failed) {
    out.status = DriveStatus::WireFailed;
    out.outcome = "transport error";
    out.message = oc.error_message;
    return out;
  }

  const auto &resp = *oc.response;
  out.data.assign(resp.data.begin(), resp.data.end());
  if (resp.status == cli::protocol::ResponseStatus::Ok) {
    out.ok = true;
    out.status = DriveStatus::Ok;
    out.outcome = "ok";
  } else {
    out.status = DriveStatus::DaemonError;
    out.outcome = resp.error ? resp.error->code : "error";
    out.message = resp.error ? resp.error->message : "daemon error";
  }
  return out;
}

auto CommandDriver::InConfigure() const -> bool {
  std::lock_guard<std::mutex> lk(mu_);
  return session_.in_configure;
}

auto CommandDriver::SessionId() const -> std::optional<std::string> {
  std::lock_guard<std::mutex> lk(mu_);
  return session_.session_id;
}

}  // namespace einheit::ui
