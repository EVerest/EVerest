// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest

#include "tls_connection_test.hpp"

#include <algorithm>
#include <cstring>
#include <poll.h>
#include <tuple>
#include <unistd.h>

// Empty derived fixture: gives these cases their own suite name so they stay
// addressable via --gtest_filter.
struct TlsClientWrapTest : TlsTest {};

TEST_F(TlsClientWrapTest, WrapConnectingFdHandshake) {
    using state_t = tls::Server::state_t;
    using result_t = tls::Connection::result_t;

    // Passed via server_config.socket so init() skips its own bind/listen path.
    const auto listener = make_loopback_listener();
    ASSERT_GE(listener.fd, 0);

    server_config.socket = listener.fd;
    const auto init_state = server.init(server_config, nullptr);
    ASSERT_TRUE(init_state == state_t::init_complete || init_state == state_t::init_socket);

    std::thread server_side([&]() {
        auto server_conn = accept_and_wrap(server, listener.fd);
        if (server_conn) {
            std::ignore = server_conn->accept(1000);
            std::ignore = server_conn->shutdown(1000);
        }
    });

    const int client_fd = connect_loopback_nonblocking(listener.port);
    ASSERT_GE(client_fd, 0);

    ClientTest local_client;
    local_client.init(client_config);

    auto conn = local_client.wrap_connecting_fd(client_fd, "localhost");
    ASSERT_TRUE(conn);
    EXPECT_EQ(conn->socket(), client_fd);

    const auto drive = drive_client_handshake(*conn, client_fd);
    EXPECT_EQ(drive.result, result_t::success);
    EXPECT_NE(conn->peer_certificate(), nullptr);

    std::ignore = conn->shutdown(1000);

    if (server_side.joinable()) {
        server_side.join();
    }
    std::ignore = ::close(listener.fd);
}

// The PKI server leaf carries DNS:localhost, so pinning to "localhost" passes.
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
            std::ignore = server_conn->accept(1000);
            std::ignore = server_conn->shutdown(1000);
        }
    });

    const int client_fd = connect_loopback_nonblocking(listener.port);
    ASSERT_GE(client_fd, 0);

    tls::Client::config_t verify_config = client_config;
    verify_config.verify_subject_name = true;

    ClientTest local_client;
    local_client.init(verify_config);

    auto conn = local_client.wrap_connecting_fd(client_fd, "localhost");
    ASSERT_TRUE(conn);

    const auto drive = drive_client_handshake(*conn, client_fd);
    EXPECT_EQ(drive.result, result_t::success);
    EXPECT_NE(conn->peer_certificate(), nullptr);

    std::ignore = conn->shutdown(1000);

    if (server_side.joinable()) {
        server_side.join();
    }
    std::ignore = ::close(listener.fd);
}

// Covers the IP-literal pinning branch, which is otherwise never executed: the peer
// is named by an IP so the pin goes through X509_VERIFY_PARAM_set1_ip_asc and must
// match the leaf's iPAddress SAN entry. A negative IP test is deliberately absent, it
// would fail whichever pinning call the code made and so could not tell them apart.
TEST_F(TlsClientWrapTest, WrapConnectingFdVerifiesIpLiteral) {
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
            std::ignore = server_conn->accept(1000);
            std::ignore = server_conn->shutdown(1000);
        }
    });

    const int client_fd = connect_loopback_nonblocking(listener.port);
    ASSERT_GE(client_fd, 0);

    tls::Client::config_t verify_config = client_config;
    verify_config.verify_subject_name = true;

    ClientTest local_client;
    local_client.init(verify_config);

    auto conn = local_client.wrap_connecting_fd(client_fd, "127.0.0.1");
    ASSERT_TRUE(conn);

    const auto drive = drive_client_handshake(*conn, client_fd);
    EXPECT_EQ(drive.result, result_t::success);
    EXPECT_NE(conn->peer_certificate(), nullptr);

    std::ignore = conn->shutdown(1000);

    if (server_side.joinable()) {
        server_side.join();
    }
    std::ignore = ::close(listener.fd);
}

// The server leaf carries DNS:localhost and DNS:evse.pionix.de, so
// "wrong.example" matches no subject/SAN entry.
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
            std::ignore = server_conn->accept(1000);
            std::ignore = server_conn->shutdown(1000);
        }
    });

    const int client_fd = connect_loopback_nonblocking(listener.port);
    ASSERT_GE(client_fd, 0);

    tls::Client::config_t verify_config = client_config;
    verify_config.verify_subject_name = true;

    ClientTest local_client;
    local_client.init(verify_config);

    auto conn = local_client.wrap_connecting_fd(client_fd, "wrong.example");
    ASSERT_TRUE(conn);

    const auto drive = drive_client_handshake(*conn, client_fd);
    EXPECT_NE(drive.result, result_t::success);

    std::ignore = conn->shutdown(1000);

    if (server_side.joinable()) {
        server_side.join();
    }
    std::ignore = ::close(listener.fd);
}

// A non-numeric service string must not throw out of ServerConnection; only key
// logging is skipped. The CTX-wide keylog callback still fires for such a
// connection, so the handshake is driven to completion to cover that path.
TEST_F(TlsClientWrapTest, WrapAcceptedFdToleratesNonNumericService) {
    using state_t = tls::Server::state_t;
    using result_t = tls::Connection::result_t;

    const auto listener = make_loopback_listener();
    ASSERT_GE(listener.fd, 0);

    server_config.socket = listener.fd;
    server_config.tls_key_logging = true;
    server_config.tls_key_logging_path = ::testing::TempDir();
    const auto init_state = server.init(server_config, nullptr);
    ASSERT_TRUE(init_state == state_t::init_complete || init_state == state_t::init_socket);

    // Empty service string.
    {
        const int client_fd = connect_loopback_nonblocking(listener.port);
        ASSERT_GE(client_fd, 0);

        const int accepted_fd = ::accept(listener.fd, nullptr, nullptr);
        ASSERT_GE(accepted_fd, 0);

        tls::Server::ConnectionPtr conn;
        ASSERT_NO_THROW(conn = server.wrap_accepted_fd(accepted_fd, "::1", ""));
        ASSERT_TRUE(conn);

        auto server_result = result_t::timeout;
        std::thread server_side([&]() {
            server_result = conn->accept(1000);
            std::ignore = conn->shutdown(1000);
        });

        client.init(client_config);
        auto client_conn = client.wrap_connecting_fd(client_fd, "localhost");
        ASSERT_TRUE(client_conn);

        const auto drive = drive_client_handshake(*client_conn, client_fd);
        EXPECT_EQ(drive.result, result_t::success);

        std::ignore = client_conn->shutdown(1000);

        if (server_side.joinable()) {
            server_side.join();
        }
        EXPECT_EQ(server_result, result_t::success);
    }

    // Garbage service string.
    {
        const int client_fd = connect_loopback_nonblocking(listener.port);
        ASSERT_GE(client_fd, 0);

        const int accepted_fd = ::accept(listener.fd, nullptr, nullptr);
        ASSERT_GE(accepted_fd, 0);

        tls::Server::ConnectionPtr conn;
        ASSERT_NO_THROW(conn = server.wrap_accepted_fd(accepted_fd, "::1", "not-a-port"));
        EXPECT_TRUE(conn);

        // conn owns accepted_fd, only the client fd needs closing here.
        std::ignore = ::close(client_fd);
    }

    std::ignore = ::close(listener.fd);
}

// A reader draining a message must be able to tell that decrypted plaintext is
// still buffered. One TLS record can carry more plaintext than the caller's
// buffer holds, so a short read leaves the remainder in the record layer, and
// the socket will not necessarily become readable again to deliver it. A drain
// loop keyed only on socket readability would strand that remainder.
TEST_F(TlsClientWrapTest, WrapConnectingFdReportsBufferedPlaintext) {
    using state_t = tls::Server::state_t;
    using result_t = tls::Connection::result_t;

    const auto listener = make_loopback_listener();
    ASSERT_GE(listener.fd, 0);

    server_config.socket = listener.fd;
    const auto init_state = server.init(server_config, nullptr);
    ASSERT_TRUE(init_state == state_t::init_complete || init_state == state_t::init_socket);

    // Sent with a single write(), hence a single TLS record, and read back in
    // chunks far smaller than the record's plaintext.
    constexpr std::size_t payload_size = 512;
    constexpr std::size_t chunk_size = 64;
    std::array<std::byte, payload_size> payload{};
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<std::byte>('a' + (i % 26));
    }

    std::thread server_side([&]() {
        auto server_conn = accept_and_wrap(server, listener.fd);
        if (!server_conn) {
            return;
        }
        if (server_conn->accept(1000) == result_t::success) {
            std::size_t sent = 0;
            std::ignore = server_conn->write(payload.data(), payload.size(), sent, 1000);
        }
        std::ignore = server_conn->shutdown(1000);
    });

    const int client_fd = connect_loopback_nonblocking(listener.port);
    ASSERT_GE(client_fd, 0);

    ClientTest local_client;
    local_client.init(client_config);

    auto conn = local_client.wrap_connecting_fd(client_fd, "localhost");
    ASSERT_TRUE(conn);

    const auto drive = drive_client_handshake(*conn, client_fd);
    ASSERT_EQ(drive.result, result_t::success);

    std::array<std::byte, payload_size> received{};
    std::size_t got = 0;
    EXPECT_EQ(conn->read(received.data(), chunk_size, got, 1000), result_t::success);
    EXPECT_EQ(got, chunk_size);
    EXPECT_LT(got, payload.size());

    // The record was decrypted in full, so its remainder is held by the record
    // layer rather than by the socket.
    EXPECT_TRUE(conn->has_pending());

    std::size_t total = got;
    auto drain_result = result_t::success;
    while (total < payload.size() && drain_result == result_t::success) {
        std::size_t chunk = 0;
        const auto want = std::min(chunk_size, payload.size() - total);
        drain_result = conn->read(received.data() + total, want, chunk, 1000);
        total += chunk;
    }
    EXPECT_EQ(drain_result, result_t::success);
    EXPECT_EQ(total, payload.size());
    EXPECT_EQ(std::memcmp(received.data(), payload.data(), payload.size()), 0);

    EXPECT_FALSE(conn->has_pending());

    std::ignore = conn->shutdown(1000);

    if (server_side.joinable()) {
        server_side.join();
    }
    std::ignore = ::close(listener.fd);
}

TEST_F(TlsClientWrapTest, WrapConnectingFdNonBlocking) {
    using state_t = tls::Server::state_t;
    using result_t = tls::Connection::result_t;

    const auto listener = make_loopback_listener();
    ASSERT_GE(listener.fd, 0);

    server_config.socket = listener.fd;
    const auto init_state = server.init(server_config, nullptr);
    ASSERT_TRUE(init_state == state_t::init_complete || init_state == state_t::init_socket);

    // Echo 4 bytes back so the client's non-blocking write/read paths run.
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
                std::ignore = server_conn->write(buf.data(), buf.size(), sent, 1000);
            }
        }
        std::ignore = server_conn->shutdown(1000);
    });

    const int client_fd = connect_loopback_nonblocking(listener.port);
    ASSERT_GE(client_fd, 0);

    // timeout 0 forces purely non-blocking operation.
    tls::Client::config_t nb_config = client_config;
    nb_config.io_timeout_ms = 0;

    ClientTest local_client;
    local_client.init(nb_config);

    auto conn = local_client.wrap_connecting_fd(client_fd, "localhost");
    ASSERT_TRUE(conn);

    const auto drive = drive_client_handshake(*conn, client_fd, 0, 200, 500);
    ASSERT_EQ(drive.result, result_t::success);

    EXPECT_TRUE(drive.want_read > 0 || drive.want_write > 0)
        << "expected at least one want_read or want_write during non-blocking handshake";

    const std::array<std::byte, 4> ping{std::byte{'p'}, std::byte{'i'}, std::byte{'n'}, std::byte{'g'}};
    std::size_t written = 0;
    for (int i = 0; i < 50 && written < ping.size(); ++i) {
        const auto wres = conn->write(ping.data(), ping.size(), written, 0);
        if (wres == result_t::want_write) {
            pollfd pfd{client_fd, POLLOUT, 0};
            std::ignore = ::poll(&pfd, 1, 100);
        } else if (wres == result_t::want_read) {
            pollfd pfd{client_fd, POLLIN, 0};
            std::ignore = ::poll(&pfd, 1, 100);
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
            std::ignore = ::poll(&pfd, 1, 500);
        } else if (rres == result_t::want_write) {
            pollfd pfd{client_fd, POLLOUT, 0};
            std::ignore = ::poll(&pfd, 1, 500);
        } else {
            break;
        }
    }
    ASSERT_EQ(echoed, echo.size());
    EXPECT_EQ(std::memcmp(echo.data(), ping.data(), ping.size()), 0);

    result_t sr = result_t::timeout;
    for (int i = 0; i < 50 && sr != result_t::closed; ++i) {
        sr = conn->shutdown(0);
        if (sr == result_t::want_read) {
            pollfd pfd{client_fd, POLLIN, 0};
            std::ignore = ::poll(&pfd, 1, 200);
        } else if (sr == result_t::want_write) {
            pollfd pfd{client_fd, POLLOUT, 0};
            std::ignore = ::poll(&pfd, 1, 200);
        }
    }

    if (server_side.joinable()) {
        server_side.join();
    }
    std::ignore = ::close(listener.fd);
}
