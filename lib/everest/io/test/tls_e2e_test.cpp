// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// End-to-end test: a tls_listener (server) AND a tls_client (the alias) driven
// on the SAME single fd_event_handler loop, on one thread. The client's TCP
// connect runs on a detached worker thread; the listen backlog buffers the SYN
// so the loop accepts it once polling starts. Both TLS handshakes are then
// driven on the one loop (nested fd_event_sync_interface model). This exercises
// the full > 4096-byte drain path and RFC-6125 hostname verification.

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/tls/tls_client.hpp>
#include <everest/io/tls/tls_listener.hpp>
#include <everest/io/tls/tls_server.hpp>
#include <everest/io/tls/tls_server_socket.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace std::chrono_literals;

namespace {

namespace io = everest::lib::io;

using tls_payload = io::tls::tls_client_socket::PayloadT;

/// Payload larger than a single 4096-byte TLS record so the rx() path must
/// drain SSL_pending() to return the whole message.
constexpr std::size_t kLargePayload = 10000;

/// Build a > 4096-byte patterned payload for round-trip verification.
std::vector<std::uint8_t> make_large_payload() {
    std::vector<std::uint8_t> payload(kLargePayload);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<std::uint8_t>((i * 31 + 7) & 0xFF);
    }
    return payload;
}

/// Populate a tls_listener::Config with the test PKI and an ephemeral bind on
/// 127.0.0.1. Mirrors the listener config used in tls_listener_test.cpp.
io::tls::tls_listener::Config make_listener_config() {
    io::tls::tls_listener::Config lcfg;
    lcfg.tls.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
    lcfg.tls.ciphersuites = "";
    auto& chain = lcfg.tls.chains.emplace_back();
    chain.certificate_chain_file = "server_chain.pem";
    chain.private_key_file = "server_priv.pem";
    chain.trust_anchor_file = "server_root_cert.pem";
    chain.ocsp_response_files = {"ocsp_response.der", "ocsp_response.der"};
    lcfg.tls.verify_client = false;
    lcfg.tls.io_timeout_ms = 1000;
    lcfg.bind_addr = "127.0.0.1";
    lcfg.bind_port = 0;
    lcfg.ipv6_only = false;
    return lcfg;
}

/// Base client config that trusts the test server root so chain validation
/// passes. Hostname verification is left off by default (opt in per test).
io::tls::tls_client_socket::Config make_client_config() {
    io::tls::tls_client_socket::Config cfg;
    cfg.tls.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
    cfg.tls.ciphersuites = "";
    cfg.tls.verify_locations_file = "server_root_cert.pem";
    cfg.tls.io_timeout_ms = 2000;
    cfg.tls.verify_server = true;
    return cfg;
}

} // namespace

// A tls_listener (server) and a tls_client (the alias) share one
// fd_event_handler on one thread. The client sends a > 4096-byte payload, the
// server echoes it, and the loop drives both TLS handshakes plus the round-trip.
// Asserts the FULL payload round-trips intact (size + byte content).
TEST(TlsE2E, RoundTripLargePayloadSingleLoop) {
    io::tls::tls_listener listener(make_listener_config());
    const auto port = listener.listen_port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";

    io::event::fd_event_handler ev;

    // The yielded tls_server must outlive the accept callback; the loop drives
    // its handshake/rx/tx after register_event_handler().
    std::unique_ptr<io::tls::tls_server> server_conn;
    listener.set_accept_callback(
        [&](std::unique_ptr<io::tls::tls_server> srv, std::string /*ip*/, std::uint16_t /*peer_port*/) {
            srv->set_rx_handler([](io::tls::tls_server_socket::PayloadT const& payload, auto& self) {
                self.tx(payload); // echo
            });
            ev.register_event_handler(srv.get());
            server_conn = std::move(srv);
        });
    ASSERT_TRUE(ev.register_event_handler(&listener));

    const auto expected = make_large_payload();

    io::tls::tls_client client(make_client_config(), std::string("127.0.0.1"), port, 2000);

    std::atomic<bool> running{true};
    std::vector<std::uint8_t> echo;
    echo.reserve(kLargePayload);

    client.set_on_ready_action([&client, &expected]() {
        tls_payload msg(expected.begin(), expected.end());
        client.tx(msg);
    });
    client.set_rx_handler([&echo, &running](tls_payload const& payload, auto&) {
        echo.insert(echo.end(), payload.begin(), payload.end());
        if (echo.size() >= kLargePayload) {
            running = false;
        }
    });
    ev.register_event_handler(&client);

    // Timed poll so the deadline is checked every tick even with no events; a
    // stall fails the test deterministically rather than hanging.
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (running && std::chrono::steady_clock::now() < deadline) {
        ev.poll(50ms);
        ev.run_actions();
    }
    running = false;

    ASSERT_EQ(echo.size(), kLargePayload) << "round-trip did not complete within 5 seconds (drain or echo failed)";
    EXPECT_EQ(echo, expected) << "echoed payload differs from sent payload";
}

// Hostname verification SUCCESS: verify_subject_name=true with host_for_sni
// "localhost". The server leaf cert carries a DNS:localhost SAN, so RFC-6125
// verification passes even though the TCP connect target is the 127.0.0.1
// literal (SNI is the matched name, not the connect target). Round-trip
// completes.
TEST(TlsE2E, HostnameVerificationSucceeds) {
    io::tls::tls_listener listener(make_listener_config());
    const auto port = listener.listen_port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";

    io::event::fd_event_handler ev;

    std::unique_ptr<io::tls::tls_server> server_conn;
    listener.set_accept_callback(
        [&](std::unique_ptr<io::tls::tls_server> srv, std::string /*ip*/, std::uint16_t /*peer_port*/) {
            srv->set_rx_handler([](io::tls::tls_server_socket::PayloadT const& payload, auto& self) {
                self.tx(payload); // echo
            });
            ev.register_event_handler(srv.get());
            server_conn = std::move(srv);
        });
    ASSERT_TRUE(ev.register_event_handler(&listener));

    auto cfg = make_client_config();
    cfg.tls.verify_subject_name = true;
    cfg.host_for_sni = "localhost";

    io::tls::tls_client client(cfg, std::string("127.0.0.1"), port, 2000);

    std::atomic<bool> running{true};
    std::atomic<bool> got_echo{false};
    const std::vector<std::uint8_t> ping = {'p', 'i', 'n', 'g'};
    std::vector<std::uint8_t> echo;

    client.set_on_ready_action([&client, &ping]() {
        tls_payload msg(ping.begin(), ping.end());
        client.tx(msg);
    });
    client.set_rx_handler([&echo, &ping, &got_echo, &running](tls_payload const& payload, auto&) {
        echo.insert(echo.end(), payload.begin(), payload.end());
        if (echo.size() >= ping.size()) {
            got_echo = true;
            running = false;
        }
    });
    ev.register_event_handler(&client);

    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (running && std::chrono::steady_clock::now() < deadline) {
        ev.poll(50ms);
        ev.run_actions();
    }
    running = false;

    EXPECT_TRUE(got_echo) << "round-trip did not complete with verify_subject_name + correct SNI within 5 seconds";
    EXPECT_EQ(echo, ping) << "echoed payload differs from sent payload";
}

// Hostname verification FAILURE: verify_subject_name=true with a host_for_sni
// that does NOT match any SAN on the server leaf cert. The TLS handshake must
// fail verification; success here is observing the client error (no hang). The
// round-trip must NOT complete.
TEST(TlsE2E, WrongHostnameVerificationFails) {
    io::tls::tls_listener listener(make_listener_config());
    const auto port = listener.listen_port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";

    io::event::fd_event_handler ev;

    std::unique_ptr<io::tls::tls_server> server_conn;
    listener.set_accept_callback(
        [&](std::unique_ptr<io::tls::tls_server> srv, std::string /*ip*/, std::uint16_t /*peer_port*/) {
            srv->set_rx_handler([](io::tls::tls_server_socket::PayloadT const& payload, auto& self) {
                self.tx(payload); // echo
            });
            ev.register_event_handler(srv.get());
            server_conn = std::move(srv);
        });
    ASSERT_TRUE(ev.register_event_handler(&listener));

    auto cfg = make_client_config();
    cfg.tls.verify_subject_name = true;
    cfg.host_for_sni = "wrong.example";

    io::tls::tls_client client(cfg, std::string("127.0.0.1"), port, 2000);

    std::atomic<bool> running{true};
    std::atomic<bool> got_error{false};
    std::atomic<bool> got_echo{false};
    const std::vector<std::uint8_t> ping = {'p', 'i', 'n', 'g'};

    client.set_error_handler([&got_error, &running](int err, std::string const& /*msg*/) {
        if (err != 0) {
            got_error = true;
            running = false;
        }
    });
    client.set_on_ready_action([&client, &ping]() {
        // Should never fire: the handshake fails verification before ready.
        tls_payload msg(ping.begin(), ping.end());
        client.tx(msg);
    });
    client.set_rx_handler([&got_echo, &running](tls_payload const& /*payload*/, auto&) {
        got_echo = true;
        running = false;
    });
    ev.register_event_handler(&client);

    // Short deadline: a hostname mismatch must surface as an error promptly, not
    // hang. Success = error observed (or, at minimum, no round-trip completes).
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (running && std::chrono::steady_clock::now() < deadline) {
        ev.poll(50ms);
        ev.run_actions();
    }
    running = false;

    EXPECT_FALSE(got_echo) << "round-trip completed despite a hostname mismatch";
    EXPECT_TRUE(got_error) << "client error handler did not fire on a hostname mismatch within 3 seconds";
}

// Steady-state teardown: after a successful handshake and round-trip the server
// drops its connection. The client's next rx fails inside the connection-fd
// dispatch lambda, which calls fail(). fail() must NOT remove its own fd
// synchronously from inside that dispatch pass (that would erase the
// std::function currently executing and resize the pollfds mid-iteration).
// Removal is deferred to run_actions(). Success = the error handler fires and
// the loop keeps running without crashing after teardown.
TEST(TlsE2E, ServerCloseTearsDownClientWithoutCrash) {
    io::tls::tls_listener listener(make_listener_config());
    const auto port = listener.listen_port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";

    io::event::fd_event_handler ev;

    std::unique_ptr<io::tls::tls_server> server_conn;
    listener.set_accept_callback(
        [&](std::unique_ptr<io::tls::tls_server> srv, std::string /*ip*/, std::uint16_t /*peer_port*/) {
            srv->set_rx_handler([](io::tls::tls_server_socket::PayloadT const& payload, auto& self) {
                self.tx(payload); // echo, then the test drops the server below
            });
            ev.register_event_handler(srv.get());
            server_conn = std::move(srv);
        });
    ASSERT_TRUE(ev.register_event_handler(&listener));

    io::tls::tls_client client(make_client_config(), std::string("127.0.0.1"), port, 2000);

    std::atomic<bool> running{true};
    std::atomic<bool> got_error{false};
    std::atomic<bool> got_echo{false};
    const std::vector<std::uint8_t> ping = {'p', 'i', 'n', 'g'};

    client.set_on_ready_action([&client, &ping]() {
        tls_payload msg(ping.begin(), ping.end());
        client.tx(msg);
    });
    client.set_rx_handler([&got_echo](tls_payload const& /*payload*/, auto&) { got_echo = true; });
    client.set_error_handler([&got_error](int err, std::string const& /*msg*/) {
        if (err != 0) {
            got_error = true;
        }
    });
    ev.register_event_handler(&client);

    // Phase 1: drive the handshake + round-trip, then tear down the server so the
    // client observes a peer close on its next rx.
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    bool dropped = false;
    while (running && std::chrono::steady_clock::now() < deadline) {
        ev.poll(50ms);
        ev.run_actions();
        if (got_echo && not dropped) {
            // Drop the server connection: closes the peer fd so the client rx
            // fails and routes through fail() on its next loop wakeup.
            ev.unregister_event_handler(server_conn.get());
            server_conn.reset();
            dropped = true;
        }
        if (got_error) {
            running = false;
        }
    }
    running = false;

    EXPECT_TRUE(got_echo) << "round-trip did not complete before the server was dropped";
    EXPECT_TRUE(got_error) << "client error handler did not fire after the server closed within 5 seconds";

    // Keep polling after teardown: a synchronous fd removal from inside the
    // dispatch pass would have corrupted the handler state; a deferred removal
    // leaves the loop healthy. No crash here is the assertion.
    for (int i = 0; i < 5; ++i) {
        ev.poll(10ms);
        ev.run_actions();
    }
}

// A payload enqueued via tx() right after register_event_handler() returns —
// before the TCP connect has yielded a connection fd, let alone before the
// handshake completed — must be held and flushed once the handshake finishes.
// The eventfd tx-notify fired while m_fd was still -1, so the flush depends on
// the handshake-completion path re-arming POLLOUT for the queued payload.
TEST(TlsE2E, tx_queued_before_handshake_is_flushed_after_ready) {
    io::tls::tls_listener listener(make_listener_config());
    const auto port = listener.listen_port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";

    io::event::fd_event_handler ev;

    std::unique_ptr<io::tls::tls_server> server_conn;
    listener.set_accept_callback(
        [&](std::unique_ptr<io::tls::tls_server> srv, std::string /*ip*/, std::uint16_t /*peer_port*/) {
            srv->set_rx_handler([](io::tls::tls_server_socket::PayloadT const& payload, auto& self) {
                self.tx(payload); // echo
            });
            ev.register_event_handler(srv.get());
            server_conn = std::move(srv);
        });
    ASSERT_TRUE(ev.register_event_handler(&listener));

    io::tls::tls_client client(make_client_config(), std::string("127.0.0.1"), port, 2000);

    std::atomic<bool> running{true};
    const std::vector<std::uint8_t> ping = {'p', 'i', 'n', 'g'};
    std::vector<std::uint8_t> echo;

    client.set_rx_handler([&echo, &ping, &running](tls_payload const& payload, auto&) {
        echo.insert(echo.end(), payload.begin(), payload.end());
        if (echo.size() >= ping.size()) {
            running = false;
        }
    });
    ev.register_event_handler(&client);

    // Send BEFORE the handshake (or even the TCP connect) has completed: no
    // on-ready action involved. The payload must be queued and flushed later.
    tls_payload msg(ping.begin(), ping.end());
    ASSERT_TRUE(client.tx(msg)) << "tx() rejected a payload queued before the handshake";

    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (running && std::chrono::steady_clock::now() < deadline) {
        ev.poll(50ms);
        ev.run_actions();
    }
    running = false;

    ASSERT_EQ(echo.size(), ping.size()) << "payload queued before the handshake was never flushed within 5 seconds";
    EXPECT_EQ(echo, ping) << "echoed payload differs from sent payload";
}

// Explicit unregister of a tls_client must run the client stop() hook so the
// m_connected event it registered in start() is torn down alongside the base
// connection fd / tx-notify. Regression guard for the register/unregister
// symmetry fix: the client is registered, driven to ready, then unregistered,
// and the handler is left to poll on. A clean unregister return plus a healthy
// post-teardown loop is the assertion.
TEST(TlsE2E, UnregisterClientRunsStopHook) {
    io::tls::tls_listener listener(make_listener_config());
    const auto port = listener.listen_port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";

    io::event::fd_event_handler ev;

    std::unique_ptr<io::tls::tls_server> server_conn;
    listener.set_accept_callback(
        [&](std::unique_ptr<io::tls::tls_server> srv, std::string /*ip*/, std::uint16_t /*peer_port*/) {
            srv->set_rx_handler([](io::tls::tls_server_socket::PayloadT const& payload, auto& self) {
                self.tx(payload); // echo
            });
            ev.register_event_handler(srv.get());
            server_conn = std::move(srv);
        });
    ASSERT_TRUE(ev.register_event_handler(&listener));

    {
        io::tls::tls_client client(make_client_config(), std::string("127.0.0.1"), port, 2000);

        std::atomic<bool> ready{false};
        client.set_on_ready_action([&ready]() { ready = true; });
        ASSERT_TRUE(ev.register_event_handler(&client));

        // Drive until the client's handshake completes (its on-ready fires).
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (not ready && std::chrono::steady_clock::now() < deadline) {
            ev.poll(50ms);
            ev.run_actions();
        }
        ASSERT_TRUE(ready) << "client handshake did not complete within 5 seconds";

        // Explicit teardown: must remove the connection fd, tx-notify, AND the
        // m_connected event the client registered in start().
        EXPECT_TRUE(ev.unregister_event_handler(&client))
            << "unregistering the client did not cleanly remove its handler entries";
    } // client destroyed here, while ev outlives it.

    // The handler outlives the client: it must hold no leftover handler that
    // references the destroyed client. Polling stays healthy. No crash here is
    // the assertion.
    for (int i = 0; i < 5; ++i) {
        ev.poll(10ms);
        ev.run_actions();
    }
}
