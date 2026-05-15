// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/tls/tls_server_socket.hpp>
#include <everest/tls/tls.hpp>

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <string>
#include <thread>

namespace {

/// Translate poll_events to POSIX poll bitmask.
short to_poll_mask(everest::lib::io::event::poll_events ev) {
    using everest::lib::io::event::poll_events;
    switch (ev) {
    case poll_events::read:
        return POLLIN;
    case poll_events::write:
        return POLLOUT;
    default:
        return POLLIN;
    }
}

/// Block until fd is ready for the requested event (max 2s).
bool poll_until(int fd, everest::lib::io::event::poll_events ev) {
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = to_poll_mask(ev);
    return ::poll(&pfd, 1, 2000) > 0;
}

} // namespace

TEST(TlsServerSocket, HandshakeAndExchange) {
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

    // Resolve actual port
    sockaddr_in bound{};
    socklen_t bound_len = sizeof(bound);
    ASSERT_EQ(::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&bound), &bound_len), 0);
    const uint16_t port = ntohs(bound.sin_port);
    const std::string port_str = std::to_string(port);

    // -----------------------------------------------------------------------
    // 2. Configure tls::Server and initialise TLS (socket provided externally)
    // -----------------------------------------------------------------------
    tls::Server::config_t cfg;
    cfg.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
    cfg.ciphersuites = "";
    auto& chain = cfg.chains.emplace_back();
    chain.certificate_chain_file = "server_chain.pem";
    chain.private_key_file = "server_priv.pem";
    chain.trust_anchor_file = "server_root_cert.pem";
    chain.ocsp_response_files = {"ocsp_response.der", "ocsp_response.der"};
    cfg.host = "127.0.0.1";
    cfg.service = port_str.c_str();
    cfg.ipv6_only = false;
    cfg.verify_client = false;
    cfg.io_timeout_ms = 1000;
    cfg.socket = listen_fd; // bypass init_socket()

    tls::Server server;
    const auto state = server.init(cfg, nullptr);
    ASSERT_NE(state, tls::Server::state_t::init_needed)
        << "tls::Server::init failed — check that server_chain.pem etc. exist "
           "in the working directory (lib/everest/tls/tests in the build tree).";

    // -----------------------------------------------------------------------
    // 3. Background TLS client: connect, write, read echo, shutdown
    // -----------------------------------------------------------------------
    std::atomic<bool> client_ok{false};
    std::string client_error;
    std::thread client_thread([&]() {
        tls::Client client;
        tls::Client::config_t ccfg;
        ccfg.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
        ccfg.verify_locations_file = "server_root_cert.pem";
        ccfg.io_timeout_ms = 2000;
        ccfg.verify_server = true;
        if (!client.init(ccfg)) {
            client_error = "client.init failed";
            return;
        }
        auto conn = client.connect("127.0.0.1", port_str.c_str(), false, 2000);
        if (!conn) {
            client_error = "client.connect returned nullptr";
            return;
        }
        if (conn->connect() != tls::Connection::result_t::success) {
            client_error = "TLS handshake failed on client side";
            return;
        }
        // Write a small payload
        const uint8_t msg[] = {'h', 'e', 'l', 'l', 'o'};
        std::size_t written = 0;
        if (conn->write(reinterpret_cast<const std::byte*>(msg), sizeof msg, written, 2000) !=
            tls::Connection::result_t::success) {
            client_error = "client write failed";
            return;
        }
        // Read the echo
        std::byte buf[64]{};
        std::size_t nread = 0;
        if (conn->read(buf, sizeof buf, nread, 2000) != tls::Connection::result_t::success) {
            client_error = "client read failed";
            return;
        }
        conn->shutdown();
        client_ok = true;
    });

    // -----------------------------------------------------------------------
    // 4. Server side: accept raw TCP fd, wrap, construct policy, handshake
    // -----------------------------------------------------------------------
    sockaddr peer{};
    socklen_t peer_len = sizeof(peer);
    int accepted_fd = ::accept(listen_fd, &peer, &peer_len);
    ASSERT_GE(accepted_fd, 0);

    // Resolve peer address strings
    char ip_buf[NI_MAXHOST] = "127.0.0.1";
    char svc_buf[NI_MAXSERV] = "0";
    ::getnameinfo(&peer, peer_len, ip_buf, sizeof ip_buf, svc_buf, sizeof svc_buf, NI_NUMERICHOST | NI_NUMERICSERV);

    auto conn = server.wrap_accepted_fd(accepted_fd, ip_buf, svc_buf);
    ASSERT_TRUE(conn) << "wrap_accepted_fd returned nullptr";

    // -----------------------------------------------------------------------
    // 5. Construct class under test
    // -----------------------------------------------------------------------
    everest::lib::io::tls::tls_server_socket sock{std::move(conn)};

    // -----------------------------------------------------------------------
    // 6. Drive handshake
    // -----------------------------------------------------------------------
    int iterations = 0;
    while (!sock.handshake_complete()) {
        ASSERT_LT(iterations++, 100) << "handshake exceeded 100 iterations";
        ASSERT_TRUE(poll_until(sock.get_fd(), sock.desired_events()));
        bool progress = sock.handshake_step();
        if (!progress && !sock.handshake_complete()) {
            FAIL() << "handshake_step returned false without completing";
        }
    }

    // -----------------------------------------------------------------------
    // 7. Receive data from client
    // -----------------------------------------------------------------------
    everest::lib::io::tls::tls_server_socket::PayloadT in_buf;
    bool rx_ok = false;
    for (int i = 0; i < 10 && !rx_ok; ++i) {
        ASSERT_TRUE(poll_until(sock.get_fd(), everest::lib::io::event::poll_events::read));
        rx_ok = sock.rx(in_buf);
    }
    ASSERT_TRUE(rx_ok);
    ASSERT_GT(in_buf.size(), 0u);

    // -----------------------------------------------------------------------
    // 8. Echo data back to client
    // -----------------------------------------------------------------------
    everest::lib::io::tls::tls_server_socket::PayloadT out_buf = in_buf;
    bool tx_ok = false;
    for (int i = 0; i < 10 && !tx_ok; ++i) {
        // Poll for write readiness before attempting to send.
        ASSERT_TRUE(poll_until(sock.get_fd(), everest::lib::io::event::poll_events::write));
        tx_ok = sock.tx(out_buf);
    }
    ASSERT_TRUE(tx_ok);

    // -----------------------------------------------------------------------
    // 9. Cleanup
    // -----------------------------------------------------------------------
    sock.close();
    client_thread.join();

    EXPECT_TRUE(client_ok) << "Client error: " << client_error;
    ::close(listen_fd);
}
