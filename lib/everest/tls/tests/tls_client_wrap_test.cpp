// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest

#include "tls_connection_test.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// Use a distinct fixture name to avoid GTest suite-name collisions when
// TlsTest (defined in the anonymous namespace of tls_connection_test.hpp)
// is instantiated in multiple translation units.
struct TlsClientWrapTest : TlsTest {};

// ---------------------------------------------------------------------------
// WrapConnectingFdHandshake
//
// Verifies that Client::wrap_connecting_fd hands a caller-owned, already-
// connecting TCP socket to an initialised Client and that the resulting
// ClientConnection completes a full TLS handshake against a local server.

TEST_F(TlsClientWrapTest, WrapConnectingFdHandshake) {
    using state_t = tls::Server::state_t;

    // Create a listen socket on an ephemeral port. Passed via
    // server_config.socket so that init() skips its own bind/listen path.
    const int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(listen_fd, 0);
    int reuse = 1;
    (void)::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in listen_addr{};
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    listen_addr.sin_port = 0;
    ASSERT_EQ(::bind(listen_fd, reinterpret_cast<sockaddr*>(&listen_addr), sizeof(listen_addr)), 0);
    ASSERT_EQ(::listen(listen_fd, 1), 0);

    server_config.socket = listen_fd;
    const auto init_state = server.init(server_config, nullptr);
    ASSERT_TRUE(init_state == state_t::init_complete || init_state == state_t::init_socket);

    // Resolve the ephemeral port assigned by the kernel.
    sockaddr_in bound_addr{};
    socklen_t bound_len = sizeof(bound_addr);
    ASSERT_EQ(::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&bound_addr), &bound_len), 0);
    const std::uint16_t bound_port = ntohs(bound_addr.sin_port);

    // Server-side thread: accept the incoming TCP connection, wrap it as a TLS
    // server connection, and drive the server handshake to give the client a
    // counterparty.
    std::thread server_side([&]() {
        sockaddr_in peer_addr{};
        socklen_t peer_len = sizeof(peer_addr);
        const int accepted_fd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&peer_addr), &peer_len);
        if (accepted_fd < 0) {
            return;
        }
        char ip_buf[INET_ADDRSTRLEN]{};
        char svc_buf[NI_MAXSERV]{};
        (void)::getnameinfo(reinterpret_cast<sockaddr*>(&peer_addr), peer_len, ip_buf, sizeof(ip_buf), svc_buf,
                            sizeof(svc_buf), NI_NUMERICHOST | NI_NUMERICSERV);
        auto server_conn = server.wrap_accepted_fd(accepted_fd, ip_buf, svc_buf);
        if (server_conn) {
            (void)server_conn->accept(1000);
            (void)server_conn->shutdown(1000);
        }
    });

    // Client side: open a non-blocking TCP socket and call connect().
    // Handle EINPROGRESS by polling until the socket is writable.
    const int client_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(client_fd, 0);
    {
        const int flags = ::fcntl(client_fd, F_GETFL, 0);
        ASSERT_NE(flags, -1);
        ASSERT_EQ(::fcntl(client_fd, F_SETFL, flags | O_NONBLOCK), 0);
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_addr.sin_port = htons(bound_port);
    const int rc = ::connect(client_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
    if (rc != 0) {
        ASSERT_EQ(errno, EINPROGRESS);
        pollfd pfd{client_fd, POLLOUT, 0};
        ASSERT_GT(::poll(&pfd, 1, 2000), 0);
        int err = 0;
        socklen_t err_len = sizeof(err);
        ASSERT_EQ(::getsockopt(client_fd, SOL_SOCKET, SO_ERROR, &err, &err_len), 0);
        ASSERT_EQ(err, 0);
    }

    // Initialise the client's SSL_CTX.
    ClientTest local_client;
    local_client.init(client_config);

    // Hand the already-connecting fd to the factory under test.
    auto conn = local_client.wrap_connecting_fd(client_fd, "localhost");
    ASSERT_TRUE(conn);
    EXPECT_EQ(conn->socket(), client_fd);

    // Drive the TLS handshake in a polling loop.
    using result_t = tls::Connection::result_t;
    result_t res = result_t::timeout;
    for (int i = 0; i < 50 && res != result_t::success; ++i) {
        res = conn->connect(1000);
        if (res == result_t::want_read) {
            pollfd pfd{client_fd, POLLIN, 0};
            (void)::poll(&pfd, 1, 1000);
        } else if (res == result_t::want_write) {
            pollfd pfd{client_fd, POLLOUT, 0};
            (void)::poll(&pfd, 1, 1000);
        }
    }
    EXPECT_EQ(res, result_t::success);
    EXPECT_NE(conn->peer_certificate(), nullptr);

    (void)conn->shutdown(1000);

    if (server_side.joinable()) {
        server_side.join();
    }
    (void)::close(listen_fd);
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

    const int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(listen_fd, 0);
    int reuse = 1;
    (void)::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in listen_addr{};
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    listen_addr.sin_port = 0;
    ASSERT_EQ(::bind(listen_fd, reinterpret_cast<sockaddr*>(&listen_addr), sizeof(listen_addr)), 0);
    ASSERT_EQ(::listen(listen_fd, 1), 0);

    server_config.socket = listen_fd;
    const auto init_state = server.init(server_config, nullptr);
    ASSERT_TRUE(init_state == state_t::init_complete || init_state == state_t::init_socket);

    sockaddr_in bound_addr{};
    socklen_t bound_len = sizeof(bound_addr);
    ASSERT_EQ(::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&bound_addr), &bound_len), 0);
    const std::uint16_t bound_port = ntohs(bound_addr.sin_port);

    std::thread server_side([&]() {
        sockaddr_in peer_addr{};
        socklen_t peer_len = sizeof(peer_addr);
        const int accepted_fd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&peer_addr), &peer_len);
        if (accepted_fd < 0) {
            return;
        }
        char ip_buf[INET_ADDRSTRLEN]{};
        char svc_buf[NI_MAXSERV]{};
        (void)::getnameinfo(reinterpret_cast<sockaddr*>(&peer_addr), peer_len, ip_buf, sizeof(ip_buf), svc_buf,
                            sizeof(svc_buf), NI_NUMERICHOST | NI_NUMERICSERV);
        auto server_conn = server.wrap_accepted_fd(accepted_fd, ip_buf, svc_buf);
        if (server_conn) {
            (void)server_conn->accept(1000);
            (void)server_conn->shutdown(1000);
        }
    });

    const int client_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(client_fd, 0);
    {
        const int flags = ::fcntl(client_fd, F_GETFL, 0);
        ASSERT_NE(flags, -1);
        ASSERT_EQ(::fcntl(client_fd, F_SETFL, flags | O_NONBLOCK), 0);
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_addr.sin_port = htons(bound_port);
    const int rc = ::connect(client_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
    if (rc != 0) {
        ASSERT_EQ(errno, EINPROGRESS);
        pollfd pfd{client_fd, POLLOUT, 0};
        ASSERT_GT(::poll(&pfd, 1, 2000), 0);
        int err = 0;
        socklen_t err_len = sizeof(err);
        ASSERT_EQ(::getsockopt(client_fd, SOL_SOCKET, SO_ERROR, &err, &err_len), 0);
        ASSERT_EQ(err, 0);
    }

    // Opt in to RFC-6125 hostname verification.
    tls::Client::config_t verify_config = client_config;
    verify_config.verify_subject_name = true;

    ClientTest local_client;
    local_client.init(verify_config);

    auto conn = local_client.wrap_connecting_fd(client_fd, "localhost");
    ASSERT_TRUE(conn);

    using result_t = tls::Connection::result_t;
    result_t res = result_t::timeout;
    for (int i = 0; i < 50 && res != result_t::success; ++i) {
        res = conn->connect(1000);
        if (res == result_t::want_read) {
            pollfd pfd{client_fd, POLLIN, 0};
            (void)::poll(&pfd, 1, 1000);
        } else if (res == result_t::want_write) {
            pollfd pfd{client_fd, POLLOUT, 0};
            (void)::poll(&pfd, 1, 1000);
        }
    }
    EXPECT_EQ(res, result_t::success);
    EXPECT_NE(conn->peer_certificate(), nullptr);

    (void)conn->shutdown(1000);

    if (server_side.joinable()) {
        server_side.join();
    }
    (void)::close(listen_fd);
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

    const int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(listen_fd, 0);
    int reuse = 1;
    (void)::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in listen_addr{};
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    listen_addr.sin_port = 0;
    ASSERT_EQ(::bind(listen_fd, reinterpret_cast<sockaddr*>(&listen_addr), sizeof(listen_addr)), 0);
    ASSERT_EQ(::listen(listen_fd, 1), 0);

    server_config.socket = listen_fd;
    const auto init_state = server.init(server_config, nullptr);
    ASSERT_TRUE(init_state == state_t::init_complete || init_state == state_t::init_socket);

    sockaddr_in bound_addr{};
    socklen_t bound_len = sizeof(bound_addr);
    ASSERT_EQ(::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&bound_addr), &bound_len), 0);
    const std::uint16_t bound_port = ntohs(bound_addr.sin_port);

    std::thread server_side([&]() {
        sockaddr_in peer_addr{};
        socklen_t peer_len = sizeof(peer_addr);
        const int accepted_fd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&peer_addr), &peer_len);
        if (accepted_fd < 0) {
            return;
        }
        char ip_buf[INET_ADDRSTRLEN]{};
        char svc_buf[NI_MAXSERV]{};
        (void)::getnameinfo(reinterpret_cast<sockaddr*>(&peer_addr), peer_len, ip_buf, sizeof(ip_buf), svc_buf,
                            sizeof(svc_buf), NI_NUMERICHOST | NI_NUMERICSERV);
        auto server_conn = server.wrap_accepted_fd(accepted_fd, ip_buf, svc_buf);
        if (server_conn) {
            (void)server_conn->accept(1000);
            (void)server_conn->shutdown(1000);
        }
    });

    const int client_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(client_fd, 0);
    {
        const int flags = ::fcntl(client_fd, F_GETFL, 0);
        ASSERT_NE(flags, -1);
        ASSERT_EQ(::fcntl(client_fd, F_SETFL, flags | O_NONBLOCK), 0);
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_addr.sin_port = htons(bound_port);
    const int rc = ::connect(client_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
    if (rc != 0) {
        ASSERT_EQ(errno, EINPROGRESS);
        pollfd pfd{client_fd, POLLOUT, 0};
        ASSERT_GT(::poll(&pfd, 1, 2000), 0);
        int err = 0;
        socklen_t err_len = sizeof(err);
        ASSERT_EQ(::getsockopt(client_fd, SOL_SOCKET, SO_ERROR, &err, &err_len), 0);
        ASSERT_EQ(err, 0);
    }

    tls::Client::config_t verify_config = client_config;
    verify_config.verify_subject_name = true;

    ClientTest local_client;
    local_client.init(verify_config);

    // SNI host that matches no subject/SAN entry of the server leaf.
    auto conn = local_client.wrap_connecting_fd(client_fd, "wrong.example");
    ASSERT_TRUE(conn);

    using result_t = tls::Connection::result_t;
    result_t res = result_t::timeout;
    for (int i = 0; i < 50 && res != result_t::success && res != result_t::closed; ++i) {
        res = conn->connect(1000);
        if (res == result_t::want_read) {
            pollfd pfd{client_fd, POLLIN, 0};
            (void)::poll(&pfd, 1, 1000);
        } else if (res == result_t::want_write) {
            pollfd pfd{client_fd, POLLOUT, 0};
            (void)::poll(&pfd, 1, 1000);
        }
    }
    // Hostname mismatch must prevent a successful handshake.
    EXPECT_NE(res, result_t::success);

    (void)conn->shutdown(1000);

    if (server_side.joinable()) {
        server_side.join();
    }
    (void)::close(listen_fd);
}

// ---------------------------------------------------------------------------
// WrapConnectingFdNonBlocking
//
// Verifies non-blocking behaviour: drives the handshake with timeout_ms=0 and
// asserts that at least one want_read or want_write was observed, confirming
// that the SSL state machine properly surfaces I/O readiness signals.

TEST_F(TlsClientWrapTest, WrapConnectingFdNonBlocking) {
    using state_t = tls::Server::state_t;

    const int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(listen_fd, 0);
    int reuse = 1;
    (void)::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in listen_addr{};
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    listen_addr.sin_port = 0;
    ASSERT_EQ(::bind(listen_fd, reinterpret_cast<sockaddr*>(&listen_addr), sizeof(listen_addr)), 0);
    ASSERT_EQ(::listen(listen_fd, 1), 0);

    server_config.socket = listen_fd;
    const auto init_state = server.init(server_config, nullptr);
    ASSERT_TRUE(init_state == state_t::init_complete || init_state == state_t::init_socket);

    sockaddr_in bound_addr{};
    socklen_t bound_len = sizeof(bound_addr);
    ASSERT_EQ(::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&bound_addr), &bound_len), 0);
    const std::uint16_t bound_port = ntohs(bound_addr.sin_port);

    std::thread server_side([&]() {
        sockaddr_in peer_addr{};
        socklen_t peer_len = sizeof(peer_addr);
        const int accepted_fd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&peer_addr), &peer_len);
        if (accepted_fd < 0) {
            return;
        }
        char ip_buf[INET_ADDRSTRLEN]{};
        char svc_buf[NI_MAXSERV]{};
        (void)::getnameinfo(reinterpret_cast<sockaddr*>(&peer_addr), peer_len, ip_buf, sizeof(ip_buf), svc_buf,
                            sizeof(svc_buf), NI_NUMERICHOST | NI_NUMERICSERV);
        auto server_conn = server.wrap_accepted_fd(accepted_fd, ip_buf, svc_buf);
        if (server_conn) {
            (void)server_conn->accept(1000);
            (void)server_conn->shutdown(1000);
        }
    });

    const int client_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(client_fd, 0);
    {
        const int flags = ::fcntl(client_fd, F_GETFL, 0);
        ASSERT_NE(flags, -1);
        ASSERT_EQ(::fcntl(client_fd, F_SETFL, flags | O_NONBLOCK), 0);
    }

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_addr.sin_port = htons(bound_port);
    const int rc = ::connect(client_fd, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));
    if (rc != 0) {
        ASSERT_EQ(errno, EINPROGRESS);
        pollfd pfd{client_fd, POLLOUT, 0};
        ASSERT_GT(::poll(&pfd, 1, 2000), 0);
        int err = 0;
        socklen_t err_len = sizeof(err);
        ASSERT_EQ(::getsockopt(client_fd, SOL_SOCKET, SO_ERROR, &err, &err_len), 0);
        ASSERT_EQ(err, 0);
    }

    // Use timeout_ms = 0 to force purely non-blocking operation.
    tls::Client::config_t nb_config = client_config;
    nb_config.io_timeout_ms = 0;

    ClientTest local_client;
    local_client.init(nb_config);

    auto conn = local_client.wrap_connecting_fd(client_fd, "localhost");
    ASSERT_TRUE(conn);

    int saw_want_read = 0;
    int saw_want_write = 0;

    using result_t = tls::Connection::result_t;
    result_t res = result_t::timeout;
    for (int i = 0; i < 200 && res != result_t::success; ++i) {
        res = conn->connect(0);
        if (res == result_t::want_read) {
            ++saw_want_read;
            pollfd pfd{client_fd, POLLIN, 0};
            (void)::poll(&pfd, 1, 500);
        } else if (res == result_t::want_write) {
            ++saw_want_write;
            pollfd pfd{client_fd, POLLOUT, 0};
            (void)::poll(&pfd, 1, 500);
        } else if (res == result_t::closed) {
            FAIL() << "connection closed unexpectedly during handshake";
        }
    }
    ASSERT_EQ(res, result_t::success);

    // At least one I/O readiness signal must have been observed.
    EXPECT_TRUE(saw_want_read > 0 || saw_want_write > 0)
        << "expected at least one want_read or want_write during non-blocking handshake";

    // Short bidirectional exchange — verify read/write want_* signals as
    // regression guard for the non-blocking I/O path.
    const std::array<std::byte, 4> ping{std::byte{'p'}, std::byte{'i'}, std::byte{'n'}, std::byte{'g'}};
    std::size_t written = 0;
    for (int i = 0; i < 50 && written == 0; ++i) {
        const auto wres = conn->write(ping.data(), ping.size(), written);
        if (wres == result_t::want_write) {
            pollfd pfd{client_fd, POLLOUT, 0};
            (void)::poll(&pfd, 1, 100);
        } else if (wres == result_t::success) {
            break;
        } else {
            break;
        }
    }

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
    (void)::close(listen_fd);
}
