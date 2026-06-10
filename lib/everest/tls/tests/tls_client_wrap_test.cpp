// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest

#include "tls_connection_test.hpp"

#include <cstring>
#include <poll.h>
#include <unistd.h>

// TlsTest (named namespace tls_test, shared across translation units) already
// hosts the connection test suite. This derived fixture exists solely to give
// the wrap_connecting_fd cases their own GoogleTest suite name so they stay
// separately addressable via --gtest_filter.
struct TlsClientWrapTest : TlsTest {};

// ---------------------------------------------------------------------------
// WrapConnectingFdHandshake
//
// Verifies that Client::wrap_connecting_fd hands a caller-owned, already-
// connecting TCP socket to an initialised Client and that the resulting
// ClientConnection completes a full TLS handshake against a local server.

TEST_F(TlsClientWrapTest, WrapConnectingFdHandshake) {
    using state_t = tls::Server::state_t;
    using result_t = tls::Connection::result_t;

    // Test-owned listen socket, passed via server_config.socket so that
    // init() skips its own bind/listen path.
    const auto listener = make_loopback_listener();
    ASSERT_GE(listener.fd, 0);

    server_config.socket = listener.fd;
    const auto init_state = server.init(server_config, nullptr);
    ASSERT_TRUE(init_state == state_t::init_complete || init_state == state_t::init_socket);

    // Server-side thread: accept, wrap, and drive the server handshake to
    // give the client a counterparty.
    std::thread server_side([&]() {
        auto server_conn = accept_and_wrap(server, listener.fd);
        if (server_conn) {
            (void)server_conn->accept(1000);
            (void)server_conn->shutdown(1000);
        }
    });

    const int client_fd = connect_loopback_nonblocking(listener.port);
    ASSERT_GE(client_fd, 0);

    // Initialise the client's SSL_CTX.
    ClientTest local_client;
    local_client.init(client_config);

    // Hand the already-connecting fd to the factory under test.
    auto conn = local_client.wrap_connecting_fd(client_fd, "localhost");
    ASSERT_TRUE(conn);
    EXPECT_EQ(conn->socket(), client_fd);

    const auto drive = drive_client_handshake(*conn, client_fd);
    EXPECT_EQ(drive.result, result_t::success);
    EXPECT_NE(conn->peer_certificate(), nullptr);

    (void)conn->shutdown(1000);

    if (server_side.joinable()) {
        server_side.join();
    }
    (void)::close(listener.fd);
}

// ---------------------------------------------------------------------------
// WrapConnectingFdVerifiesHostname
//
// With verify_subject_name = true the client pins RFC-6125 hostname
// verification (SSL_set1_host) to the SNI host. The PKI server leaf carries
// DNS:localhost, so connecting with host "localhost" must complete the
// handshake and present a peer certificate.

TEST_F(TlsClientWrapTest, WrapConnectingFdVerifiesHostname) {
    using state_t = tls::Server::state_t;
    using result_t = tls::Connection::result_t;

    const auto listener = make_loopback_listener();
    ASSERT_GE(listener.fd, 0);

    server_config.socket = listener.fd;
    const auto init_state = server.init(server_config, nullptr);
    ASSERT_TRUE(init_state == state_t::init_complete || init_state == state_t::init_socket);

    std::thread server_side([&]() {
        auto server_conn = accept_and_wrap(server, listener.fd);
        if (server_conn) {
            (void)server_conn->accept(1000);
            (void)server_conn->shutdown(1000);
        }
    });

    const int client_fd = connect_loopback_nonblocking(listener.port);
    ASSERT_GE(client_fd, 0);

    // Opt in to RFC-6125 hostname verification.
    tls::Client::config_t verify_config = client_config;
    verify_config.verify_subject_name = true;

    ClientTest local_client;
    local_client.init(verify_config);

    auto conn = local_client.wrap_connecting_fd(client_fd, "localhost");
    ASSERT_TRUE(conn);

    const auto drive = drive_client_handshake(*conn, client_fd);
    EXPECT_EQ(drive.result, result_t::success);
    EXPECT_NE(conn->peer_certificate(), nullptr);

    (void)conn->shutdown(1000);

    if (server_side.joinable()) {
        server_side.join();
    }
    (void)::close(listener.fd);
}

// ---------------------------------------------------------------------------
// WrapConnectingFdRejectsWrongHostname
//
// With verify_subject_name = true the handshake must fail when the SNI host
// does not match any subject/SAN entry of the server leaf certificate. The
// server leaf has DNS:localhost (and DNS:evse.pionix.de), so "wrong.example"
// must NOT reach result_t::success.

TEST_F(TlsClientWrapTest, WrapConnectingFdRejectsWrongHostname) {
    using state_t = tls::Server::state_t;
    using result_t = tls::Connection::result_t;

    const auto listener = make_loopback_listener();
    ASSERT_GE(listener.fd, 0);

    server_config.socket = listener.fd;
    const auto init_state = server.init(server_config, nullptr);
    ASSERT_TRUE(init_state == state_t::init_complete || init_state == state_t::init_socket);

    std::thread server_side([&]() {
        auto server_conn = accept_and_wrap(server, listener.fd);
        if (server_conn) {
            (void)server_conn->accept(1000);
            (void)server_conn->shutdown(1000);
        }
    });

    const int client_fd = connect_loopback_nonblocking(listener.port);
    ASSERT_GE(client_fd, 0);

    tls::Client::config_t verify_config = client_config;
    verify_config.verify_subject_name = true;

    ClientTest local_client;
    local_client.init(verify_config);

    // SNI host that matches no subject/SAN entry of the server leaf.
    auto conn = local_client.wrap_connecting_fd(client_fd, "wrong.example");
    ASSERT_TRUE(conn);

    const auto drive = drive_client_handshake(*conn, client_fd);
    // Hostname mismatch must prevent a successful handshake.
    EXPECT_NE(drive.result, result_t::success);

    (void)conn->shutdown(1000);

    if (server_side.joinable()) {
        server_side.join();
    }
    (void)::close(listener.fd);
}

// ---------------------------------------------------------------------------
// WrapConnectingFdNonBlocking
//
// Verifies non-blocking behaviour: drives the handshake with timeout_ms=0,
// asserts that at least one want_read or want_write was observed, and then
// completes a 4-byte echo round trip over the non-blocking read/write paths.

TEST_F(TlsClientWrapTest, WrapConnectingFdNonBlocking) {
    using state_t = tls::Server::state_t;
    using result_t = tls::Connection::result_t;

    const auto listener = make_loopback_listener();
    ASSERT_GE(listener.fd, 0);

    server_config.socket = listener.fd;
    const auto init_state = server.init(server_config, nullptr);
    ASSERT_TRUE(init_state == state_t::init_complete || init_state == state_t::init_socket);

    // Server-side thread: after the handshake, read 4 bytes and echo them
    // back so the client's non-blocking write/read paths can be asserted.
    std::thread server_side([&]() {
        auto server_conn = accept_and_wrap(server, listener.fd);
        if (!server_conn) {
            return;
        }
        if (server_conn->accept(1000) == result_t::success) {
            std::array<std::byte, 4> buf{};
            std::size_t total = 0;
            int timeouts = 0;
            while (total < buf.size() && timeouts <= 10) {
                std::size_t got = 0;
                const auto rres = server_conn->read(buf.data() + total, buf.size() - total, got, 1000);
                if (rres == result_t::success) {
                    total += got;
                } else if (rres == result_t::timeout) {
                    ++timeouts;
                } else {
                    break;
                }
            }
            if (total == buf.size()) {
                std::size_t sent = 0;
                (void)server_conn->write(buf.data(), buf.size(), sent, 1000);
            }
        }
        (void)server_conn->shutdown(1000);
    });

    const int client_fd = connect_loopback_nonblocking(listener.port);
    ASSERT_GE(client_fd, 0);

    // Use timeout_ms = 0 to force purely non-blocking operation.
    tls::Client::config_t nb_config = client_config;
    nb_config.io_timeout_ms = 0;

    ClientTest local_client;
    local_client.init(nb_config);

    auto conn = local_client.wrap_connecting_fd(client_fd, "localhost");
    ASSERT_TRUE(conn);

    const auto drive = drive_client_handshake(*conn, client_fd, 0, 200, 500);
    ASSERT_EQ(drive.result, result_t::success);

    // At least one I/O readiness signal must have been observed.
    EXPECT_TRUE(drive.want_read > 0 || drive.want_write > 0)
        << "expected at least one want_read or want_write during non-blocking handshake";

    // Echo round trip: write "ping" and read the 4 bytes back.
    const std::array<std::byte, 4> ping{std::byte{'p'}, std::byte{'i'}, std::byte{'n'}, std::byte{'g'}};
    std::size_t written = 0;
    for (int i = 0; i < 50 && written < ping.size(); ++i) {
        const auto wres = conn->write(ping.data(), ping.size(), written, 0);
        if (wres == result_t::want_write) {
            pollfd pfd{client_fd, POLLOUT, 0};
            (void)::poll(&pfd, 1, 100);
        } else if (wres == result_t::want_read) {
            pollfd pfd{client_fd, POLLIN, 0};
            (void)::poll(&pfd, 1, 100);
        } else if (wres != result_t::success) {
            break;
        }
    }
    ASSERT_EQ(written, ping.size());

    std::array<std::byte, 4> echo{};
    std::size_t echoed = 0;
    for (int i = 0; i < 50 && echoed < echo.size(); ++i) {
        std::size_t got = 0;
        const auto rres = conn->read(echo.data() + echoed, echo.size() - echoed, got, 0);
        if (rres == result_t::success) {
            echoed += got;
        } else if (rres == result_t::want_read) {
            pollfd pfd{client_fd, POLLIN, 0};
            (void)::poll(&pfd, 1, 500);
        } else if (rres == result_t::want_write) {
            pollfd pfd{client_fd, POLLOUT, 0};
            (void)::poll(&pfd, 1, 500);
        } else {
            break;
        }
    }
    ASSERT_EQ(echoed, echo.size());
    EXPECT_EQ(std::memcmp(echo.data(), ping.data(), ping.size()), 0);

    // Drain the shutdown cleanly.
    result_t sr = result_t::timeout;
    for (int i = 0; i < 50 && sr != result_t::closed; ++i) {
        sr = conn->shutdown(0);
        if (sr == result_t::want_read) {
            pollfd pfd{client_fd, POLLIN, 0};
            (void)::poll(&pfd, 1, 200);
        } else if (sr == result_t::want_write) {
            pollfd pfd{client_fd, POLLOUT, 0};
            (void)::poll(&pfd, 1, 200);
        }
    }

    if (server_side.joinable()) {
        server_side.join();
    }
    (void)::close(listener.fd);
}
