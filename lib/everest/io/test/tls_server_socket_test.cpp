// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include "tls_test_common.hpp"

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/tls/tls_server.hpp>
#include <everest/io/tls/tls_server_socket.hpp>
#include <everest/tls/tls.hpp>

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

namespace test = everest::lib::io::test;

/// Payload size that exceeds a single 4096-byte TLS-record read so the
/// server-side rx() must drain SSL_pending() records in one call.
constexpr std::size_t kLargePayload = 10000;

} // namespace

// tls_server (the loop-driven register-interface class): wrap an accepted fd,
// construct a tls_server, register it with an fd_event_handler and drive the
// loop. The handshake runs on the loop via accept(0); a > 4096-byte client
// payload must arrive in one rx() (SSL_pending drain) and be echoed back.
TEST(TlsServer, HandshakeAndExchange) {
    // -----------------------------------------------------------------------
    // 1. Create ephemeral listen socket
    // -----------------------------------------------------------------------
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(listen_fd, 0);

    int opt = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::inet_addr("127.0.0.1");
    addr.sin_port = 0; // ephemeral
    ASSERT_EQ(::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
    ASSERT_EQ(::listen(listen_fd, 1), 0);

    sockaddr_in bound{};
    socklen_t bound_len = sizeof(bound);
    ASSERT_EQ(::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&bound), &bound_len), 0);
    const uint16_t port = ntohs(bound.sin_port);
    const std::string port_str = std::to_string(port);

    // -----------------------------------------------------------------------
    // 2. Configure tls::Server and initialise TLS (socket provided externally)
    // -----------------------------------------------------------------------
    auto cfg = test::server_test_config();
    cfg.host = "127.0.0.1";
    cfg.service = port_str.c_str();
    cfg.ipv6_only = false;
    cfg.socket = listen_fd; // bypass init_socket()

    tls::Server server;
    const auto state = server.init(cfg, nullptr);
    ASSERT_NE(state, tls::Server::state_t::init_needed)
        << "tls::Server::init failed — check that server_chain.pem etc. exist "
           "in the working directory (lib/everest/tls/tests in the build tree).";

    // -----------------------------------------------------------------------
    // 3. Background TLS client: connect, write large payload, read echo back
    // -----------------------------------------------------------------------
    std::vector<uint8_t> msg(kLargePayload);
    for (std::size_t i = 0; i < msg.size(); ++i) {
        msg[i] = static_cast<uint8_t>(i & 0xFF);
    }
    std::atomic<bool> client_ok{false};
    std::string client_error;
    std::thread client_thread(
        [&]() { test::run_blocking_echo_client("127.0.0.1", port, msg, client_ok, client_error); });

    // -----------------------------------------------------------------------
    // 4. Server side: accept raw TCP fd, wrap, construct tls_server.
    // -----------------------------------------------------------------------
    sockaddr peer{};
    socklen_t peer_len = sizeof(peer);
    int accepted_fd = ::accept(listen_fd, &peer, &peer_len);
    ASSERT_GE(accepted_fd, 0);

    char ip_buf[NI_MAXHOST] = "127.0.0.1";
    char svc_buf[NI_MAXSERV] = "0";
    ::getnameinfo(&peer, peer_len, ip_buf, sizeof ip_buf, svc_buf, sizeof svc_buf, NI_NUMERICHOST | NI_NUMERICSERV);

    auto conn = server.wrap_accepted_fd(accepted_fd, ip_buf, svc_buf);
    ASSERT_TRUE(conn) << "wrap_accepted_fd returned nullptr";

    everest::lib::io::tls::tls_server srv(std::move(conn));

    // -----------------------------------------------------------------------
    // 5. Drive the loop: handshake (accept(0)) + large rx drain + tx echo.
    // -----------------------------------------------------------------------
    everest::lib::io::event::fd_event_handler ev;

    std::atomic<bool> rx_fired{false};
    std::vector<std::uint8_t> in_buf;

    srv.set_rx_handler(
        [&in_buf, &rx_fired](everest::lib::io::tls::tls_server_socket::PayloadT const& payload, auto& self) {
            rx_fired = true;
            in_buf.insert(in_buf.end(), payload.begin(), payload.end());
            self.tx(payload); // echo
        });
    ASSERT_TRUE(ev.register_event_handler(&srv));

    // Drive the loop until the client confirms the full echo round-trip (it
    // verifies the payload itself), or the deadline expires. Stopping when the
    // server merely RECEIVES would race the not-yet-drained echo write.
    test::pump_until(
        ev, [&] { return client_ok.load(); }, 5s);

    client_thread.join();
    ::close(listen_fd);

    EXPECT_TRUE(rx_fired) << "server-side rx handler did not fire";
    ASSERT_EQ(in_buf.size(), kLargePayload) << "rx() did not drain all buffered TLS records in one call";
    for (std::size_t i = 0; i < in_buf.size(); ++i) {
        ASSERT_EQ(in_buf[i], static_cast<uint8_t>(i & 0xFF)) << "payload corruption at index " << i;
    }
    EXPECT_TRUE(client_ok) << "Client error: " << client_error;
}
