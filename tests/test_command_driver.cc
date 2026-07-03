/// @file test_command_driver.cc
/// @brief Behavioural tests for the UI command-driver seam.
///
/// These are the acceptance tests for "UI drives the CLI command
/// engine". They wire a real confd Runtime over a MemoryBackend (a
/// fake programmable box) behind an in-process transport, then drive
/// it through the UI's CommandDriver and, in parallel, through the
/// engine the way the shell does — and prove the two front-ends are
/// indistinguishable at the engine boundary:
///
///   * MutationReachesDevice   — a UI mutation actually programs the
///     box (stub-proof: asserts on MemoryBackend::DeviceState()).
///   * AuditParity             — the UI's audit record equals the
///     CLI's for the same command.
///   * AuthParity              — a role the operator lacks is rejected
///     identically from both front-ends, and the box is untouched.
///   * CommitConfirmedReconnect — the commit-confirmed countdown is
///     visible to a fresh (reconnecting) reader and `confirm` works.
///   * RollbackThroughEngine   — rollback re-applies a prior revision.
// Copyright (c) 2026 Einheit Networks

#include "einheit/ui/command_driver.h"

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "einheit/cli/audit.h"
#include "einheit/cli/auth.h"
#include "einheit/cli/command_tree.h"
#include "einheit/cli/engine.h"
#include "einheit/cli/session.h"
#include "einheit/cli/confd/config_backend.h"
#include "einheit/cli/confd/memory_backend.h"
#include "einheit/cli/confd/runtime.h"
#include "einheit/cli/transport/inproc.h"

namespace {

namespace cli = einheit::cli;
namespace ui = einheit::ui;

/// A confd stack: a fake box (MemoryBackend), the framework runtime
/// over it, and an in-process transport bound to the runtime. Non-
/// movable members, so it is constructed in place per test.
struct ConfdStack {
  cli::confd::MemoryBackend backend{nullptr};  // null schema => free-form
  cli::confd::Runtime runtime{backend};
  cli::transport::InProcTransport tx{
      [this](const cli::protocol::Request &r) {
        return runtime.HandleRequest(r);
      }};
  ConfdStack() { (void)tx.Connect(); }
};

/// Collects the engine-side audit records a front-end emits.
struct AuditLog {
  std::vector<cli::audit::Record> records;
  auto Sink() -> cli::audit::Sink {
    return [this](const cli::audit::Record &r) { records.push_back(r); };
  }
};

auto Operator() -> cli::auth::CallerIdentity {
  return ui::MakeCaller("alice", "operator", "10.0.0.9");
}

/// Run one command the way the *shell* does: tokenise, Parse against
/// the shared tree with the caller's role, then engine::Execute. This
/// is the CLI front-end in the parity tests.
auto RunAsCli(const cli::CommandTree &tree, cli::transport::Transport *tx,
              cli::Session *session, cli::audit::Sink sink,
              const cli::auth::CallerIdentity &caller,
              const std::vector<std::string> &tokens) -> void {
  auto parsed = cli::Parse(tree, tokens, caller.role);
  ASSERT_TRUE(parsed) << "parse failed for tokens";
  cli::engine::Context ctx;
  ctx.tx = tx;
  ctx.session = session;
  ctx.caller = caller;
  ctx.audit = std::move(sink);
  (void)cli::engine::Execute(ctx, *parsed);
}

// ---------------------------------------------------------------------------
// A UI mutation actually reaches the box — stub-proof.
// ---------------------------------------------------------------------------
TEST(CommandDriver, MutationReachesDeviceThroughEngine) {
  ConfdStack s;
  AuditLog log;
  ui::CommandDriver driver(ui::BuildConfdCommandTree(), &s.tx, log.Sink());
  const auto op = Operator();

  ASSERT_TRUE(driver.Drive(op, "configure").ok);
  ASSERT_TRUE(driver.InConfigure());
  ASSERT_TRUE(driver.Drive(op, "set", {"device.hostname", "sw1"}).ok);
  ASSERT_TRUE(driver.Drive(op, "set", {"device.mtu", "9000"}).ok);
  const auto commit = driver.Drive(op, "commit");
  ASSERT_TRUE(commit.ok) << commit.outcome;
  EXPECT_FALSE(driver.InConfigure());

  // The fake box actually holds the committed values — a stub that
  // returned Ok without applying would fail here.
  const auto device = s.backend.DeviceState();
  EXPECT_EQ(device.at("device.hostname"), "sw1");
  EXPECT_EQ(device.at("device.mtu"), "9000");
  EXPECT_EQ(s.backend.ApplyCount(), 1);
  // A value never set is absent — not a blanket "everything applied".
  EXPECT_EQ(device.find("device.vlan"), device.end());

  // Exactly the mutations were audited (configure, set, set, commit),
  // each successful — reads never entered this log.
  ASSERT_EQ(log.records.size(), 4u);
  for (const auto &r : log.records) EXPECT_TRUE(r.ok) << r.command;
  EXPECT_EQ(log.records.front().command, "configure");
  EXPECT_EQ(log.records.back().command, "commit");
  EXPECT_EQ(log.records.back().user, "alice");
  EXPECT_EQ(log.records.back().role, "operator");
}

// ---------------------------------------------------------------------------
// The UI's audit record equals the CLI's for the same command.
// ---------------------------------------------------------------------------
TEST(CommandDriver, AuditParityUiVsCli) {
  // Two structurally identical fresh stacks — one driven by the UI
  // front-end, one by the CLI front-end — as the same operator.
  ConfdStack ui_side;
  ConfdStack cli_side;
  AuditLog ui_log;
  AuditLog cli_log;
  const auto op = Operator();

  ui::CommandDriver driver(ui::BuildConfdCommandTree(), &ui_side.tx,
                           ui_log.Sink());
  const auto tree = ui::BuildConfdCommandTree();
  cli::Session cli_session;

  // The identical sequence through each front-end.
  driver.Drive(op, "configure");
  RunAsCli(tree, &cli_side.tx, &cli_session, cli_log.Sink(), op,
           {"configure"});

  driver.Drive(op, "set", {"device.hostname", "sw1"});
  RunAsCli(tree, &cli_side.tx, &cli_session, cli_log.Sink(), op,
           {"set", "device.hostname", "sw1"});

  driver.Drive(op, "commit confirmed", {"5"});
  RunAsCli(tree, &cli_side.tx, &cli_session, cli_log.Sink(), op,
           {"commit", "confirmed", "5"});

  ASSERT_EQ(ui_log.records.size(), cli_log.records.size());
  ASSERT_EQ(ui_log.records.size(), 3u);
  for (std::size_t i = 0; i < ui_log.records.size(); ++i) {
    const auto &u = ui_log.records[i];
    const auto &c = cli_log.records[i];
    // Everything the audit trail is *about* must match: actor, role,
    // command, wire verb, args, session, and outcome. (Timestamps are
    // wall-clock and intentionally excluded.)
    EXPECT_EQ(u.user, c.user) << "step " << i;
    EXPECT_EQ(u.role, c.role) << "step " << i;
    EXPECT_EQ(u.command, c.command) << "step " << i;
    EXPECT_EQ(u.wire_command, c.wire_command) << "step " << i;
    EXPECT_EQ(u.args, c.args) << "step " << i;
    EXPECT_EQ(u.session_id, c.session_id) << "step " << i;
    EXPECT_EQ(u.ok, c.ok) << "step " << i;
    EXPECT_EQ(u.outcome, c.outcome) << "step " << i;
  }
  // And the sequence really was the config lifecycle.
  EXPECT_EQ(ui_log.records[0].wire_command, "configure");
  EXPECT_EQ(ui_log.records[1].wire_command, "set");
  EXPECT_EQ(ui_log.records[2].wire_command, "commit_confirmed");
}

// ---------------------------------------------------------------------------
// A forbidden role is rejected identically from both front-ends.
// ---------------------------------------------------------------------------
TEST(CommandDriver, AuthParityForbiddenActionRejectedIdentically) {
  ConfdStack ui_side;
  ConfdStack cli_side;
  AuditLog ui_log;
  AuditLog cli_log;
  // A read-only operator may not commit.
  const auto viewer = ui::MakeCaller("bob", "readonly");

  ui::CommandDriver driver(ui::BuildConfdCommandTree(), &ui_side.tx,
                           ui_log.Sink());
  const auto res = driver.Drive(viewer, "commit");
  EXPECT_FALSE(res.ok);
  EXPECT_EQ(res.status, ui::DriveStatus::RoleForbidden);
  EXPECT_EQ(res.outcome, "role forbidden");

  // The CLI front-end: the engine is the authoritative gate, so even
  // reaching Execute with a hand-built command is rejected the same.
  const auto tree = ui::BuildConfdCommandTree();
  cli::Session cli_session;
  cli::ParsedCommand parsed;
  parsed.spec = &tree.by_path.at("commit");
  cli::engine::Context ctx;
  ctx.tx = &cli_side.tx;
  ctx.session = &cli_session;
  ctx.caller = viewer;
  ctx.audit = cli_log.Sink();
  const auto cli_res = cli::engine::Execute(ctx, parsed);
  ASSERT_FALSE(cli_res);
  EXPECT_EQ(cli_res.error().code, cli::engine::EngineError::RoleForbidden);

  // Both emitted one rejection record, identical in the fields that
  // matter, and neither touched the box.
  ASSERT_EQ(ui_log.records.size(), 1u);
  ASSERT_EQ(cli_log.records.size(), 1u);
  EXPECT_FALSE(ui_log.records[0].ok);
  EXPECT_FALSE(cli_log.records[0].ok);
  EXPECT_EQ(ui_log.records[0].outcome, cli_log.records[0].outcome);
  EXPECT_EQ(ui_log.records[0].outcome, "role forbidden");
  EXPECT_EQ(ui_log.records[0].user, cli_log.records[0].user);
  EXPECT_EQ(ui_side.backend.ApplyCount(), 0);
  EXPECT_EQ(cli_side.backend.ApplyCount(), 0);
}

// ---------------------------------------------------------------------------
// Commit-confirmed countdown survives a reconnect; confirm holds it.
// ---------------------------------------------------------------------------
TEST(CommandDriver, CommitConfirmedCountdownVisibleAndConfirmable) {
  ConfdStack s;
  AuditLog log;
  ui::CommandDriver driver(ui::BuildConfdCommandTree(), &s.tx, log.Sink());
  const auto op = Operator();

  ASSERT_TRUE(driver.Drive(op, "configure").ok);
  ASSERT_TRUE(driver.Drive(op, "set", {"device.hostname", "risky"}).ok);
  const auto cc = driver.Drive(op, "commit confirmed", {"5"});
  ASSERT_TRUE(cc.ok) << cc.outcome;
  // The daemon echoes the commit id and the remaining window.
  EXPECT_NE(cc.data.find("commit_id="), std::string::npos);
  EXPECT_NE(cc.data.find("confirm_within_s="), std::string::npos);

  // A *reconnecting* browser holds no prior state — it reads the live
  // pending-confirm window straight off the runtime (the fast direct
  // read path) and sees a positive countdown and what to confirm.
  const auto pending = s.runtime.PendingConfirmState();
  ASSERT_TRUE(pending.armed);
  EXPECT_GT(pending.pending_commit, 0u);
  EXPECT_GT(pending.deadline_epoch_ms, 0);

  // Confirming holds the config: the window disarms, nothing reverts,
  // and no re-apply happens (ApplyCount stays at the single commit).
  const auto confirm = driver.Drive(op, "confirm");
  ASSERT_TRUE(confirm.ok) << confirm.outcome;
  EXPECT_FALSE(s.runtime.PendingConfirmState().armed);
  EXPECT_EQ(s.backend.DeviceState().at("device.hostname"), "risky");
  EXPECT_EQ(s.backend.ApplyCount(), 1);
}

// ---------------------------------------------------------------------------
// Rollback re-applies a prior committed revision through the engine.
// ---------------------------------------------------------------------------
TEST(CommandDriver, RollbackThroughEngine) {
  ConfdStack s;
  AuditLog log;
  ui::CommandDriver driver(ui::BuildConfdCommandTree(), &s.tx, log.Sink());
  const auto op = Operator();

  // Commit A: hostname = alpha.
  ASSERT_TRUE(driver.Drive(op, "configure").ok);
  ASSERT_TRUE(driver.Drive(op, "set", {"device.hostname", "alpha"}).ok);
  ASSERT_TRUE(driver.Drive(op, "commit").ok);
  // Commit B: hostname = bravo.
  ASSERT_TRUE(driver.Drive(op, "configure").ok);
  ASSERT_TRUE(driver.Drive(op, "set", {"device.hostname", "bravo"}).ok);
  ASSERT_TRUE(driver.Drive(op, "commit").ok);
  EXPECT_EQ(s.backend.DeviceState().at("device.hostname"), "bravo");

  // Roll back to the previous revision — the box goes back to alpha.
  const auto rb = driver.Drive(op, "rollback previous");
  ASSERT_TRUE(rb.ok) << rb.outcome;
  EXPECT_EQ(s.backend.DeviceState().at("device.hostname"), "alpha");
  // Three real applies reached the box: A, B, and the rollback.
  EXPECT_EQ(s.backend.ApplyCount(), 3);
}

}  // namespace
