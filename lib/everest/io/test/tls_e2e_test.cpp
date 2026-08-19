// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// End-to-end: a tls_listener and a tls_client on one fd_event_handler, on one thread. Only the
// client's TCP connect runs off-loop, on a detached worker; the listen backlog buffers the SYN so
// the loop accepts it once polling starts. Both TLS handshakes then run on that one loop.

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

/// Exceeds the 4096-byte rx() chunk buffer, so rx() must drain the remainder via has_pending().
constexpr std::size_t kLargePayload = 10000;

std::vector<std::uint8_t> make_large_payload() {
    std::vector<std::uint8_t> payload(kLargePayload);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<std::uint8_t>((i * 31 + 7) & 0xFF);
    }
    return payload;
}

} // namespace

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
    client.set_rx_handler([&echo, &running](tls_payload const& payload, io::tls::tls_client_interface&) {
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

// The server leaf cert carries a DNS:localhost SAN, so RFC-6125 verification passes even though the
// TCP connect target is the 127.0.0.1 literal: SNI is the matched name, not the connect target.
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
    client.set_rx_handler(
        [&echo, &ping, &got_echo, &running](tls_payload const& payload, io::tls::tls_client_interface&) {
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

// host_for_sni matches no SAN on the server leaf cert, so verification must fail and report.
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
    // Written on the loop thread, read on the same thread below, so no atomic is needed.
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
    client.set_rx_handler([&got_echo, &running](tls_payload const& /*payload*/, io::tls::tls_client_interface&) {
        got_echo = true;
        running = false;
    });
    ev.register_event_handler(&client);

    // Short deadline: a mismatch must surface promptly rather than hang.
    test::pump_until(
        ev, [&] { return !running; }, 3s);

    EXPECT_FALSE(got_echo) << "round-trip completed despite a hostname mismatch";
    EXPECT_TRUE(got_error) << "client error handler did not fire on a hostname mismatch within 3 seconds";
    EXPECT_FALSE(error_text.empty()) << "error handler received an empty string instead of the OpenSSL error text";
}

// The client's rx fails inside the connection-fd dispatch lambda, which must NOT remove its own fd
// synchronously from that pass: it would erase the executing std::function and resize the pollfds
// mid-iteration. The teardown is deferred to a queued action.
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
    client.set_rx_handler(
        [&got_echo](tls_payload const& /*payload*/, io::tls::tls_client_interface&) { got_echo = true; });
    client.set_error_handler([&got_error](int err, std::string const& /*msg*/) {
        if (err != 0) {
            got_error = true;
        }
    });
    ev.register_event_handler(&client);

    bool dropped = false;
    test::pump_until(
        ev,
        [&] {
            if (got_echo && not dropped) {
                // Closes the peer fd, so the client rx fails and routes through fail().
                ev.unregister_event_handler(rig.server_conn.get());
                rig.server_conn.reset();
                dropped = true;
            }
            return got_error.load();
        },
        5s);

    EXPECT_TRUE(got_echo) << "round-trip did not complete before the server was dropped";
    EXPECT_TRUE(got_error) << "client error handler did not fire after the server closed within 5 seconds";

    // No crash here is the assertion.
    for (int i = 0; i < 5; ++i) {
        ev.poll(10ms);
        ev.run_actions();
    }
}

// The accept callback runs before the accepted connection's handshake, so a payload queued there
// consumes its one-shot tx-notify while the fd is monitored for the handshake alone. Without a
// re-notify on handshake completion the payload never leaves the queue.
TEST(TlsE2E, server_tx_queued_before_accept_handshake_is_flushed) {
    // Declared first so it outlives every endpoint, whose destructor unregisters from it.
    io::event::fd_event_handler ev;

    io::tls::tls_listener listener(test::listener_test_config());
    const auto port = listener.listen_port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";

    const std::vector<std::uint8_t> greeting = {'h', 'e', 'l', 'l', 'o'};

    std::unique_ptr<io::tls::tls_server> server_conn;
    listener.set_accept_callback(
        [&](std::unique_ptr<io::tls::tls_server> srv, std::string /*ip*/, std::uint16_t /*peer_port*/) {
            // Queued before the accept handshake has even started.
            EXPECT_TRUE(srv->tx(greeting));
            ev.register_event_handler(srv.get());
            server_conn = std::move(srv);
        });
    ASSERT_TRUE(ev.register_event_handler(&listener));

    io::tls::tls_client client(test::client_test_config(), std::string("127.0.0.1"), port, 2000);

    std::vector<std::uint8_t> received;
    client.set_rx_handler([&received](tls_payload const& payload, io::tls::tls_client_interface&) {
        received.insert(received.end(), payload.begin(), payload.end());
    });
    ASSERT_TRUE(ev.register_event_handler(&client));

    test::pump_until(
        ev, [&] { return received.size() >= greeting.size(); }, 5s);

    ASSERT_EQ(received.size(), greeting.size())
        << "payload queued before the accept handshake was never flushed within 5 seconds";
    EXPECT_EQ(received, greeting) << "delivered payload differs from the queued one";
}

// Two distinct buffering windows. Before the transport is connected, the generic client accepts for
// an async connect policy until an attempt has actually failed. Once connected, the tx-notify fires
// while the fd is monitored for the handshake alone, so the completion path must re-arm write.
TEST(TlsE2E, tx_queued_before_handshake_is_flushed_after_ready) {
    io::event::fd_event_handler ev;
    auto rig = test::make_echo_listener(ev);
    const auto port = rig.port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";
    ASSERT_TRUE(rig.registered);

    io::tls::tls_client client(test::client_test_config(), std::string("127.0.0.1"), port, 2000);

    std::atomic<bool> running{true};
    const std::vector<std::uint8_t> ping = {'p', 'i', 'n', 'g'};
    const std::vector<std::uint8_t> pong = {'p', 'o', 'n', 'g'};
    std::vector<std::uint8_t> echo;

    client.set_rx_handler([&echo, &ping, &pong, &running](tls_payload const& payload, io::tls::tls_client_interface&) {
        echo.insert(echo.end(), payload.begin(), payload.end());
        if (echo.size() >= ping.size() + pong.size()) {
            running = false;
        }
    });
    ASSERT_TRUE(ev.register_event_handler(&client));

    // Before the connect: no connection fd exists yet, so this can only be buffered.
    tls_payload first(ping.begin(), ping.end());
    ASSERT_TRUE(client.tx(first)) << "tx() rejected a payload queued before the connect completed";

    // A cleared error state means the connect result was processed. The handshake cannot be done
    // yet: its first step only sends the ClientHello, and the server shares this loop.
    ASSERT_TRUE(test::pump_until(
        ev, [&] { return client.get_raw_handler() != nullptr && !client.on_error(); }, 5s))
        << "the TCP connect never completed";
    ASSERT_NE(client.get_raw_handler(), nullptr);
    ASSERT_FALSE(client.get_raw_handler()->handshake_complete())
        << "the handshake already completed, so this is no longer the queue-across-the-handshake window";

    // During the handshake: a second window, distinct from the one above.
    tls_payload second(pong.begin(), pong.end());
    ASSERT_TRUE(client.tx(second)) << "tx() rejected a payload queued during the handshake";

    test::pump_until(
        ev, [&] { return !running; }, 5s);

    std::vector<std::uint8_t> expected = ping;
    expected.insert(expected.end(), pong.begin(), pong.end());
    ASSERT_EQ(echo.size(), expected.size()) << "payloads queued across the handshake were not both flushed";
    EXPECT_EQ(echo, expected) << "echoed payloads differ from the sent ones, or arrived out of order";
}

// Observed through is_registered(), because unregister_event_handler() returns true either way.
// The destructor drops the registration too, so this is the explicit path, not the only one.
TEST(TlsE2E, UnregisterClientRemovesPollFd) {
    io::event::fd_event_handler ev;
    auto rig = test::make_echo_listener(ev);
    const auto port = rig.port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";
    ASSERT_TRUE(rig.registered);

    int poll_fd = -1;
    {
        io::tls::tls_client client(test::client_test_config(), std::string("127.0.0.1"), port, 2000);

        std::atomic<bool> ready{false};
        client.set_on_ready_action([&ready]() { ready = true; });
        ASSERT_TRUE(ev.register_event_handler(&client));

        test::pump_until(
            ev, [&] { return ready.load(); }, 5s);
        ASSERT_TRUE(ready) << "client handshake did not complete within 5 seconds";

        poll_fd = client.get_poll_fd();
        ASSERT_GE(poll_fd, 0);
        EXPECT_TRUE(ev.unregister_event_handler(&client));
    } // client destroyed here, while ev outlives it.

    for (int i = 0; i < 5; ++i) {
        ev.poll(10ms);
        ev.run_actions();
    }

    EXPECT_FALSE(ev.is_registered(poll_fd))
        << "the client's nested poll fd is still in the handler map after unregister";
}

// libtls reports a peer close as result_t::closed whether or not the session was shut down, so the
// discriminator is last_error(), empty only on a graceful close and surfaced as the error text.
TEST(TlsE2E, client_teardown_closes_the_tls_session) {
    io::event::fd_event_handler ev;
    auto rig = test::make_echo_listener(ev);
    const auto port = rig.port();
    ASSERT_GT(port, 0u) << "listener bound to port 0 unexpectedly";
    ASSERT_TRUE(rig.registered);

    std::atomic<bool> server_closed{false};
    std::string server_error_text{"unset"};

    const std::vector<std::uint8_t> ping = {'p', 'i', 'n', 'g'};
    {
        io::tls::tls_client client(test::client_test_config(), std::string("127.0.0.1"), port, 2000);

        std::atomic<bool> echoed{false};
        client.set_rx_handler([&echoed](tls_payload const&, io::tls::tls_client_interface&) { echoed = true; });
        ASSERT_TRUE(ev.register_event_handler(&client));

        // The accepted endpoint only exists once the listener has run its accept callback.
        ASSERT_TRUE(test::pump_until(
            ev, [&] { return rig.server_conn != nullptr; }, 5s))
            << "the listener never accepted a connection";
        rig.server_conn->set_error_handler([&](int, std::string const& text) {
            server_error_text = text;
            server_closed = true;
        });

        // Proves both sides finished the handshake, so the close below is not an aborted negotiation.
        tls_payload payload(ping.begin(), ping.end());
        ASSERT_TRUE(client.tx(payload));
        ASSERT_TRUE(test::pump_until(
            ev, [&] { return echoed.load(); }, 5s))
            << "the round trip never completed";
    } // client destroyed here, while ev and the listener outlive it.

    ASSERT_TRUE(test::pump_until(
        ev, [&] { return server_closed.load(); }, 5s))
        << "the server never observed the client going away";
    EXPECT_EQ(server_error_text, std::string{})
        << "the client dropped the socket without closing the TLS session, so the peer cannot tell "
           "this from a truncated stream: "
        << server_error_text;
}
