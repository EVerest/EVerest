// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// End-to-end test: a tls_listener (server) AND a tls_client (the alias) driven
// on the SAME single fd_event_handler loop, on one thread. The client's TCP
// connect runs on a detached worker thread; the listen backlog buffers the SYN
// so the loop accepts it once polling starts. Both TLS handshakes are then
// driven on the one loop (nested fd_event_sync_interface model). This exercises
// the full > 4096-byte drain path and RFC-6125 hostname verification.

#include "tls_test_common.hpp"

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
namespace test = everest::lib::io::test;

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

} // namespace

// A tls_listener (server) and a tls_client (the alias) share one
// fd_event_handler on one thread. The client sends a > 4096-byte payload, the
// server echoes it, and the loop drives both TLS handshakes plus the round-trip.
// Asserts the FULL payload round-trips intact (size + byte content).
TEST(TlsE2E, RoundTripLargePayloadSingleLoop) {
    io::event::fd_event_handler ev;
    auto rig = test::make_echo_listener(ev);
    const auto port = rig.port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";
    ASSERT_TRUE(rig.registered);

    const auto expected = make_large_payload();

    io::tls::tls_client client(test::client_test_config(), std::string("127.0.0.1"), port, 2000);

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

    test::pump_until(
        ev, [&] { return !running; }, 5s);

    ASSERT_EQ(echo.size(), kLargePayload) << "round-trip did not complete within 5 seconds (drain or echo failed)";
    EXPECT_EQ(echo, expected) << "echoed payload differs from sent payload";
}

// Hostname verification SUCCESS: verify_subject_name=true with host_for_sni
// "localhost". The server leaf cert carries a DNS:localhost SAN, so RFC-6125
// verification passes even though the TCP connect target is the 127.0.0.1
// literal (SNI is the matched name, not the connect target). Round-trip
// completes.
TEST(TlsE2E, HostnameVerificationSucceeds) {
    io::event::fd_event_handler ev;
    auto rig = test::make_echo_listener(ev);
    const auto port = rig.port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";
    ASSERT_TRUE(rig.registered);

    auto cfg = test::client_test_config(2000, "localhost");
    cfg.tls.verify_subject_name = true;

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

    test::pump_until(
        ev, [&] { return !running; }, 5s);

    EXPECT_TRUE(got_echo) << "round-trip did not complete with verify_subject_name + correct SNI within 5 seconds";
    EXPECT_EQ(echo, ping) << "echoed payload differs from sent payload";
}

// Hostname verification FAILURE: verify_subject_name=true with a host_for_sni
// that does NOT match any SAN on the server leaf cert. The TLS handshake must
// fail verification; success here is observing the client error (no hang). The
// round-trip must NOT complete.
TEST(TlsE2E, WrongHostnameVerificationFails) {
    io::event::fd_event_handler ev;
    auto rig = test::make_echo_listener(ev);
    const auto port = rig.port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";
    ASSERT_TRUE(rig.registered);

    auto cfg = test::client_test_config(2000, "wrong.example");
    cfg.tls.verify_subject_name = true;

    io::tls::tls_client client(cfg, std::string("127.0.0.1"), port, 2000);

    std::atomic<bool> running{true};
    std::atomic<bool> got_error{false};
    std::atomic<bool> got_echo{false};
    const std::vector<std::uint8_t> ping = {'p', 'i', 'n', 'g'};
    // The handler runs on the loop thread, same thread as the assertions below,
    // so a plain string capture is safe.
    std::string error_text;

    client.set_error_handler([&got_error, &running, &error_text](int err, std::string const& msg) {
        if (err != 0) {
            got_error = true;
            error_text = msg;
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
    test::pump_until(
        ev, [&] { return !running; }, 3s);

    EXPECT_FALSE(got_echo) << "round-trip completed despite a hostname mismatch";
    EXPECT_TRUE(got_error) << "client error handler did not fire on a hostname mismatch within 3 seconds";
    EXPECT_FALSE(error_text.empty()) << "error handler received an empty string instead of the OpenSSL error text";
}

// Steady-state teardown: after a successful handshake and round-trip the server
// drops its connection. The client's next rx fails inside the connection-fd
// dispatch lambda, which calls fail(). fail() must NOT remove its own fd
// synchronously from inside that dispatch pass (that would erase the
// std::function currently executing and resize the pollfds mid-iteration).
// Removal is deferred to run_actions(). Success = the error handler fires and
// the loop keeps running without crashing after teardown.
TEST(TlsE2E, ServerCloseTearsDownClientWithoutCrash) {
    io::event::fd_event_handler ev;
    auto rig = test::make_echo_listener(ev);
    const auto port = rig.port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";
    ASSERT_TRUE(rig.registered);

    io::tls::tls_client client(test::client_test_config(), std::string("127.0.0.1"), port, 2000);

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
    bool dropped = false;
    test::pump_until(
        ev,
        [&] {
            if (got_echo && not dropped) {
                // Drop the server connection: closes the peer fd so the client rx
                // fails and routes through fail() on its next loop wakeup.
                ev.unregister_event_handler(rig.server_conn.get());
                rig.server_conn.reset();
                dropped = true;
            }
            return got_error.load();
        },
        5s);

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
    io::event::fd_event_handler ev;
    auto rig = test::make_echo_listener(ev);
    const auto port = rig.port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";
    ASSERT_TRUE(rig.registered);

    io::tls::tls_client client(test::client_test_config(), std::string("127.0.0.1"), port, 2000);

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

    test::pump_until(
        ev, [&] { return !running; }, 5s);

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
    io::event::fd_event_handler ev;
    auto rig = test::make_echo_listener(ev);
    const auto port = rig.port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";
    ASSERT_TRUE(rig.registered);

    {
        io::tls::tls_client client(test::client_test_config(), std::string("127.0.0.1"), port, 2000);

        std::atomic<bool> ready{false};
        client.set_on_ready_action([&ready]() { ready = true; });
        ASSERT_TRUE(ev.register_event_handler(&client));

        // Drive until the client's handshake completes (its on-ready fires).
        test::pump_until(
            ev, [&] { return ready.load(); }, 5s);
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
