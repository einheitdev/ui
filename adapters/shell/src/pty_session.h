/// @file pty_session.h
/// @brief One PTY-bridged process per WebSocket connection. The
/// session owns the launcher child, the master fd, and the reader
/// thread that pumps PTY output back into the WebSocket.
///
/// Internal to the shell adapter (no public-API export); kept in a
/// private header so the .cc stays under 80-col line limits.
// Copyright (c) 2026 Einheit Networks

#ifndef ADAPTERS_SHELL_SRC_PTY_SESSION_H_
#define ADAPTERS_SHELL_SRC_PTY_SESSION_H_

#include <atomic>
#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <thread>

#include "einheit/ui/error.h"

namespace einheit::adapters::shell {

/// Errors specific to the PTY layer. All return paths through
/// `Spawn` use this; runtime I/O failures (write to a closed
/// master, etc.) are silent and trigger a normal session close.
enum class PtyError {
  /// `forkpty` returned -1.
  ForkPtyFailed,
  /// Child execve never completed (we observed an immediate exit
  /// in the small window we wait after fork).
  ExecCheckFailed,
  /// Reader thread could not start.
  ReaderStartFailed,
};

/// Argv values forwarded to einheit-shell-launcher per session.
/// Mirrors `ShellConfig` but only the fields the launcher cares
/// about. The adapter constructs one of these from `ShellConfig`
/// for each new WebSocket connection.
struct PtyLaunchSpec {
  std::string launcher_path;
  std::string cli_path;
  std::string adapter;
  std::string role;
  std::string user;
  unsigned int uid = 0;
  unsigned int gid = 0;
  std::string target;
  std::string endpoint;
  std::string event_endpoint;
  std::string term = "xterm-256color";
  std::string lang = "C.UTF-8";
  bool learn = false;
};

/// Per-session PTY bridge. One instance per WebSocket connection.
class PtySession {
 public:
  /// Sink invoked from the reader thread on each chunk read from
  /// the PTY master. The adapter wires this to
  /// `conn.send_text(chunk)`. Crow guarantees `send_text` is safe
  /// to call from any thread.
  using ByteSink = std::function<void(std::string_view)>;

  PtySession();
  ~PtySession();

  PtySession(const PtySession &) = delete;
  auto operator=(const PtySession &) -> PtySession & = delete;

  /// forkpty + execve einheit-shell-launcher in the child; start
  /// the reader thread in the parent. Idempotent — calling twice
  /// returns success without re-spawning.
  /// @param spec Launcher argv values.
  /// @param sink Where to push PTY-master output.
  auto Spawn(const PtyLaunchSpec &spec, ByteSink sink)
      -> std::expected<void, ui::Error<PtyError>>;

  /// Write user input bytes to the PTY master. No-op if the
  /// session has been closed. Errors from the underlying write()
  /// (typically EIO if the child died) are swallowed — the close
  /// will surface naturally on the next reader iteration.
  /// @param bytes Raw input to push.
  auto Write(std::string_view bytes) -> void;

  /// Resize the PTY (TIOCSWINSZ) so the cli sees the new
  /// dimensions. xterm.js fit-addon computes these client-side
  /// and the WS onmessage handler decodes them out of the JSON
  /// resize control message.
  /// @param rows Number of terminal rows.
  /// @param cols Number of terminal columns.
  auto Resize(unsigned short rows, unsigned short cols) -> void;

  /// Tear down the session: SIGTERM the child (then SIGKILL after
  /// a short grace), close the master fd, join the reader. Safe
  /// to call multiple times.
  auto Close() -> void;

  /// True iff the cli child is still alive and the reader thread
  /// is still running. Flips to false after the child exits (cli
  /// `exit` verb, EOF on the master). The adapter checks this on
  /// every onmessage to decide whether to forward keystrokes or
  /// to respawn.
  auto IsRunning() const -> bool { return running_.load(); }

 private:
  int master_fd_ = -1;
  int child_pid_ = -1;
  std::atomic<bool> running_{false};
  std::thread reader_;
  ByteSink sink_;
};

}  // namespace einheit::adapters::shell

#endif  // ADAPTERS_SHELL_SRC_PTY_SESSION_H_
