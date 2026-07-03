/// @file test_server_resilience.cc
/// @brief Live-server adversarial tests. Spins a real Crow app on a
/// loopback port, then proves the server survives the failure modes
/// the hardening effort targets: a route handler that throws, and a
/// stream of malformed bytes on the socket. After each assault a
/// known-good request must still succeed — that "still up" check is
/// the whole point.
// Copyright (c) 2026 Einheit Networks

#include <arpa/inet.h>
#include <crow.h>
#include <gtest/gtest.h>
#include <httplib.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>

#include "einheit/ui/server.h"

namespace einheit::ui {
namespace {

// A per-process-unique-ish loopback port to keep parallel ctest jobs
// from colliding. Derived from pid; avoids the privileged range.
auto TestPort() -> std::uint16_t {
  return static_cast<std::uint16_t>(20000 + (::getpid() % 20000));
}

// Brings up a configured Crow app on a background thread and tears it
// down cleanly. install_signals is off so the test process doesn't
// have its signal dispositions rewritten out from under gtest.
class LiveServer {
 public:
  explicit LiveServer(bool debug_errors = false) : port_(TestPort()) {
    ServerConfig cfg;
    cfg.install_signals = false;
    cfg.debug_errors = debug_errors;
    // No assets dir -> Configure skips the static mount; it still
    // installs the server-wide exception handler, which is the SUT.
    auto r = Configure(app_, cfg);
    EXPECT_TRUE(r.has_value());

    CROW_ROUTE(app_, "/ok")
    ([] { return crow::response(200, "ok"); });

    // Fault injection: a handler that throws. Crow routes it through
    // the framework's exception_handler installed by Configure().
    CROW_ROUTE(app_, "/boom")([]() -> crow::response {
      throw std::runtime_error("injected handler fault");
    });

    // A handler that throws a non-std exception.
    CROW_ROUTE(app_, "/boom-weird")([]() -> crow::response { throw 12345; });

    app_.bindaddr("127.0.0.1").port(port_);
    fut_ = app_.run_async();
    app_.wait_for_server_start();
  }

  ~LiveServer() {
    app_.stop();
    if (fut_.valid()) fut_.wait();
  }

  auto Client() -> httplib::Client {
    httplib::Client c("127.0.0.1", port_);
    c.set_connection_timeout(std::chrono::seconds(2));
    c.set_read_timeout(std::chrono::seconds(2));
    return c;
  }

  auto Port() const -> std::uint16_t {
    return port_;
  }

 private:
  std::uint16_t port_;
  crow::SimpleApp app_;
  std::future<void> fut_;
};

TEST(ServerResilience, ThrowingHandlerBecomes500AndServerStaysUp) {
  LiveServer srv;
  auto cli = srv.Client();

  auto boom = cli.Get("/boom");
  ASSERT_TRUE(boom);
  EXPECT_EQ(boom->status, 500);

  // The server survived the throw: a subsequent request succeeds.
  auto ok = cli.Get("/ok");
  ASSERT_TRUE(ok);
  EXPECT_EQ(ok->status, 200);
  EXPECT_EQ(ok->body, "ok");
}

TEST(ServerResilience, NonStdThrowBecomes500AndServerStaysUp) {
  LiveServer srv;
  auto cli = srv.Client();

  auto boom = cli.Get("/boom-weird");
  ASSERT_TRUE(boom);
  EXPECT_EQ(boom->status, 500);

  auto ok = cli.Get("/ok");
  ASSERT_TRUE(ok);
  EXPECT_EQ(ok->status, 200);
}

TEST(ServerResilience, ProdErrorBodyDoesNotLeakExceptionText) {
  LiveServer srv(/*debug_errors=*/false);
  auto cli = srv.Client();
  auto boom = cli.Get("/boom");
  ASSERT_TRUE(boom);
  EXPECT_EQ(boom->status, 500);
  // The internal exception text must not reach the client in prod.
  EXPECT_EQ(boom->body.find("injected handler fault"), std::string::npos);
}

TEST(ServerResilience, DebugErrorBodyIncludesExceptionText) {
  LiveServer srv(/*debug_errors=*/true);
  auto cli = srv.Client();
  auto boom = cli.Get("/boom");
  ASSERT_TRUE(boom);
  EXPECT_EQ(boom->status, 500);
  EXPECT_NE(boom->body.find("injected handler fault"), std::string::npos);
}

// Fire raw garbage at the listening socket, then confirm the server
// is still serving. A malformed request line, oversized junk, and a
// premature close must all be shrugged off, not crash the server.
TEST(ServerResilience, MalformedBytesDoNotCrashServer) {
  LiveServer srv;

  auto send_junk = [&](const std::string &bytes) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(fd, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(srv.Port());
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == 0) {
      (void)::write(fd, bytes.data(), bytes.size());
    }
    ::close(fd);
  };

  send_junk("not a real http request\r\n\r\n");
  send_junk(std::string(65536, '\xff'));
  send_junk("GET /ok HTTP/1.1\r\nContent-Length: 999999\r\n\r\n");
  // Embedded NUL / control bytes.
  send_junk(std::string("\x00\x01\x02\x03 garbage", 12));

  // Give Crow a moment to process/drop the junk connections.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  auto cli = srv.Client();
  auto ok = cli.Get("/ok");
  ASSERT_TRUE(ok);
  EXPECT_EQ(ok->status, 200);
}

}  // namespace
}  // namespace einheit::ui
