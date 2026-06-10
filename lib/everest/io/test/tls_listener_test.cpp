// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include "tls_test_common.hpp"

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/tls/tls_listener.hpp>
#include <everest/io/tls/tls_server.hpp>
#include <everest/io/tls/tls_server_socket.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

namespace io = everest::lib::io;
namespace test = everest::lib::io::test;

/// Bytes the client sends and expects echoed back through the server.
const std::vector<std::uint8_t> kPayload = {'p', 'i', 'n', 'g'};

/// Drive a real TLS round-trip through the listener bound to bind_addr.
///
/// A blocking TLS client (on a worker thread) connects, sends kPayload and
/// expects it echoed back. The listener accepts the connection, the accept
/// callback echoes received bytes and registers the yielded tls_server with the
/// same fd_event_handler. The main thread pumps the loop until the server-side
/// rx fires AND the client confirms the round-trip, or the 5s deadline expires.
void run_round_trip(std::string const& bind_addr, bool ipv6_only) {
    io::tls::tls_listener listener(test::listener_test_config(bind_addr, ipv6_only));

    const auto port = listener.listen_port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";

    io::event::fd_event_handler handler;

    std::atomic<bool> server_rx_fired{false};
    std::atomic<std::uint16_t> captured_peer_port{0};
    // The yielded tls_server must outlive the accept callback; the loop drives
    // its handshake/rx/tx after register_event_handler().
    std::unique_ptr<io::tls::tls_server> server_conn;

    listener.set_accept_callback(
        [&](std::unique_ptr<io::tls::tls_server> srv, std::string /*ip*/, std::uint16_t peer_port) {
            captured_peer_port = peer_port;
            srv->set_rx_handler([&server_rx_fired](io::tls::tls_server_socket::PayloadT const& payload, auto& self) {
                server_rx_fired = true;
                self.tx(payload); // echo
            });
            handler.register_event_handler(srv.get());
            server_conn = std::move(srv);
        });

    ASSERT_TRUE(handler.register_event_handler(&listener));

    std::atomic<bool> client_ok{false};
    std::string client_error;
    std::thread client_thread(
        [&]() { test::run_blocking_echo_client(bind_addr, port, kPayload, client_ok, client_error); });

    test::pump_until(
        handler, [&] { return server_rx_fired && client_ok; }, 5s);

    client_thread.join();

    EXPECT_TRUE(server_rx_fired) << "server-side rx handler did not fire within 5 seconds";
    EXPECT_TRUE(client_ok) << "client round-trip did not complete: " << client_error;
    EXPECT_GT(captured_peer_port.load(), 0u) << "peer_port not extracted from sockaddr_storage";
}

} // namespace

TEST(TlsListener, AcceptCallbackFires) {
    run_round_trip("127.0.0.1", false);
}

TEST(TlsListener, AcceptCallbackFiresIPv6) {
    run_round_trip("::1", true);
}
