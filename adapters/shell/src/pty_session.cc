/// @file pty_session.cc
/// @brief PTY bridge implementation.
// Copyright (c) 2026 Einheit Networks

#include "adapters/shell/src/pty_session.h"

#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

namespace einheit::adapters::shell {
namespace {

auto MakeError(PtyError code, std::string message)
    -> ui::Error<PtyError> {
  return ui::Error<PtyError>{code, std::move(message)};
}

// Build the launcher argv. Mirrors the cli argv builder in
// the launcher's sandbox.cc, scoped to what the launcher itself
// needs to know about.
auto BuildLauncherArgv(const PtyLaunchSpec &s)
    -> std::vector<std::string> {
  std::vector<std::string> v;
  v.push_back(s.launcher_path);
  v.push_back("--cli");
  v.push_back(s.cli_path);
  v.push_back("--uid");
  v.push_back(std::to_string(s.uid));
  v.push_back("--gid");
  v.push_back(std::to_string(s.gid));
  if (!s.user.empty()) {
    v.push_back("--user");
    v.push_back(s.user);
  }
  if (!s.role.empty()) {
    v.push_back("--role");
    v.push_back(s.role);
  }
  if (!s.adapter.empty()) {
    v.push_back("--adapter");
    v.push_back(s.adapter);
  }
  if (!s.target.empty()) {
    v.push_back("--target");
    v.push_back(s.target);
  }
  if (!s.endpoint.empty()) {
    v.push_back("--endpoint");
    v.push_back(s.endpoint);
  }
  if (!s.event_endpoint.empty()) {
    v.push_back("--event-endpoint");
    v.push_back(s.event_endpoint);
  }
  if (!s.term.empty()) {
    v.push_back("--term");
    v.push_back(s.term);
  }
  if (!s.lang.empty()) {
    v.push_back("--lang");
    v.push_back(s.lang);
  }
  return v;
}

}  // namespace

PtySession::PtySession() = default;

PtySession::~PtySession() { Close(); }

auto PtySession::Spawn(const PtyLaunchSpec &spec, ByteSink sink)
    -> std::expected<void, ui::Error<PtyError>> {
  if (running_.load()) return {};
  sink_ = std::move(sink);

  int master = -1;
  // forkpty allocates a master/slave pair, fork()s, and dup2's
  // the slave onto stdin/stdout/stderr in the child. The parent
  // gets the master fd; the child sees a fresh PTY as fd 0/1/2.
  const pid_t pid = ::forkpty(&master, nullptr, nullptr, nullptr);
  if (pid < 0) {
    return std::unexpected(
        MakeError(PtyError::ForkPtyFailed, std::strerror(errno)));
  }

  if (pid == 0) {
    // CHILD. Build argv as null-terminated C strings, then
    // execve. We pass an empty envp because the launcher's
    // ScrubEnv will rebuild the env from scratch anyway.
    const auto argv_strings = BuildLauncherArgv(spec);
    std::vector<char *> argv;
    argv.reserve(argv_strings.size() + 1);
    for (const auto &s : argv_strings) {
      argv.push_back(const_cast<char *>(s.c_str()));
    }
    argv.push_back(nullptr);
    char *envp[] = {nullptr};
    ::execve(spec.launcher_path.c_str(), argv.data(), envp);
    // execve only returns on failure. The PTY's slave is our
    // stderr; write the error so the operator sees it before the
    // session closes.
    ::dprintf(STDERR_FILENO,
              "shell adapter: execve(%s) failed: %s\r\n",
              spec.launcher_path.c_str(), ::strerror(errno));
    std::_Exit(127);
  }

  // PARENT. Master fd is non-blocking so the reader thread can
  // poll-style read(); on EOF we exit the loop and close.
  int flags = ::fcntl(master, F_GETFL, 0);
  ::fcntl(master, F_SETFL, flags | O_NONBLOCK);
  master_fd_ = master;
  child_pid_ = pid;
  running_.store(true);

  try {
    reader_ = std::thread([this] {
      std::array<char, 4096> buf{};
      while (running_.load()) {
        // poll with a short timeout so close-from-the-other-side
        // tears down quickly.
        struct pollfd p {
          master_fd_, POLLIN, 0
        };
        const int rc = ::poll(&p, 1, 200);
        if (rc < 0) {
          if (errno == EINTR) continue;
          break;
        }
        if (rc == 0) continue;
        if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) break;
        if (!(p.revents & POLLIN)) continue;
        const ssize_t n = ::read(master_fd_, buf.data(), buf.size());
        if (n > 0) {
          if (sink_) {
            sink_(std::string_view(buf.data(),
                                   static_cast<std::size_t>(n)));
          }
        } else if (n == 0) {
          // EOF — child closed its slave end.
          break;
        } else {
          if (errno == EINTR || errno == EAGAIN) continue;
          break;
        }
      }
      running_.store(false);
    });
  } catch (const std::exception &e) {
    Close();
    return std::unexpected(
        MakeError(PtyError::ReaderStartFailed, e.what()));
  }
  return {};
}

auto PtySession::Write(std::string_view bytes) -> void {
  if (!running_.load() || master_fd_ < 0) return;
  // Loop on partial writes; EAGAIN means we have to back off.
  // For the operator typing speed this almost never trips.
  std::size_t off = 0;
  while (off < bytes.size()) {
    const ssize_t n = ::write(master_fd_, bytes.data() + off,
                              bytes.size() - off);
    if (n < 0) {
      if (errno == EINTR) continue;
      if (errno == EAGAIN) {
        // Drop on the floor; PTY is congested. Caller will
        // notice via the WebSocket eventually closing if the
        // child has actually died.
        return;
      }
      return;
    }
    off += static_cast<std::size_t>(n);
  }
}

auto PtySession::Resize(unsigned short rows, unsigned short cols)
    -> void {
  if (master_fd_ < 0) return;
  struct winsize ws {};
  ws.ws_row = rows;
  ws.ws_col = cols;
  ws.ws_xpixel = 0;
  ws.ws_ypixel = 0;
  // TIOCSWINSZ on the master side is what triggers SIGWINCH on
  // the child. Failure is cosmetic; ignore.
  (void)::ioctl(master_fd_, TIOCSWINSZ, &ws);
}

auto PtySession::Close() -> void {
  if (!running_.exchange(false) && master_fd_ < 0 &&
      child_pid_ < 0) {
    return;
  }
  if (child_pid_ > 0) {
    // SIGTERM first; if the child is well-behaved (cli's exit
    // verb), it will close cleanly. Otherwise SIGKILL after a
    // short grace.
    ::kill(child_pid_, SIGTERM);
    bool reaped = false;
    for (int i = 0; i < 25; ++i) {
      int status = 0;
      const pid_t r = ::waitpid(child_pid_, &status, WNOHANG);
      if (r == child_pid_) {
        reaped = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (!reaped) {
      ::kill(child_pid_, SIGKILL);
      int status = 0;
      (void)::waitpid(child_pid_, &status, 0);
    }
    child_pid_ = -1;
  }
  if (master_fd_ >= 0) {
    ::close(master_fd_);
    master_fd_ = -1;
  }
  if (reader_.joinable()) {
    reader_.join();
  }
  spdlog::debug("shell session closed");
}

}  // namespace einheit::adapters::shell
