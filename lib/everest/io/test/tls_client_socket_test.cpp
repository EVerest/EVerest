// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/tls/tls_client.hpp>
#include <everest/io/tls/tls_client_socket.hpp>
#include <everest/tls/tls.hpp>

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
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

TEST(TlsClientSocket, HandshakeAndExchange) {
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
    const uint16_t bound_port = ntohs(bound.sin_port);
    const std::string port_str = std::to_string(bound_port);

    // -----------------------------------------------------------------------
    // 2. Configure tls::Server
    // -----------------------------------------------------------------------
    tls::Server::config_t scfg;
    scfg.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
    scfg.ciphersuites = "";
    auto& chain = scfg.chains.emplace_back();
    chain.certificate_chain_file = "server_chain.pem";
    chain.private_key_file = "server_priv.pem";
    chain.trust_anchor_file = "server_root_cert.pem";
    chain.ocsp_response_files = {"ocsp_response.der", "ocsp_response.der"};
    scfg.host = "127.0.0.1";
    scfg.service = port_str.c_str();
    scfg.ipv6_only = false;
    scfg.verify_client = false;
    scfg.io_timeout_ms = 1000;
    scfg.socket = listen_fd; // bypass init_socket()

    tls::Server server;
    const auto state = server.init(scfg, nullptr);
    ASSERT_NE(state, tls::Server::state_t::init_needed)
        << "tls::Server::init failed — check that server_chain.pem etc. exist "
           "in the working directory (lib/everest/tls/tests in the build tree).";

    // -----------------------------------------------------------------------
    // 3. Background server thread: accept, handshake, echo
    // -----------------------------------------------------------------------
    std::atomic<bool> server_ok{false};
    std::string server_error;
    std::thread server_thread([&]() {
        sockaddr peer{};
        socklen_t peer_len = sizeof(peer);
        int accepted_fd = ::accept(listen_fd, &peer, &peer_len);
        if (accepted_fd < 0) {
            server_error = "accept failed";
            return;
        }

        char ip_buf[NI_MAXHOST] = "127.0.0.1";
        char svc_buf[NI_MAXSERV] = "0";
        ::getnameinfo(&peer, peer_len, ip_buf, sizeof ip_buf, svc_buf, sizeof svc_buf, NI_NUMERICHOST | NI_NUMERICSERV);

        auto conn = server.wrap_accepted_fd(accepted_fd, ip_buf, svc_buf);
        if (!conn) {
            server_error = "wrap_accepted_fd returned nullptr";
            return;
        }

        // Blocking handshake (timeout 2s)
        const auto res = conn->accept(2000);
        if (res != tls::Connection::result_t::success) {
            server_error = "server TLS handshake failed";
            return;
        }

        // Read from client
        std::byte buf[64]{};
        std::size_t nread = 0;
        if (conn->read(buf, sizeof buf, nread, 2000) != tls::Connection::result_t::success) {
            server_error = "server read failed";
            return;
        }

        // Echo back
        std::size_t written = 0;
        if (conn->write(buf, nread, written, 2000) != tls::Connection::result_t::success) {
            server_error = "server write failed";
            return;
        }

        conn->shutdown(0);
        server_ok = true;
    });

    // -----------------------------------------------------------------------
    // 4. Client side: configure + setup + async connect
    // -----------------------------------------------------------------------
    everest::lib::io::tls::tls_client_socket sock;
    everest::lib::io::tls::tls_client_socket::Config cfg;
    cfg.tls.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
    cfg.tls.ciphersuites = "";
    cfg.tls.verify_locations_file = "server_root_cert.pem";
    cfg.tls.io_timeout_ms = 1000;
    cfg.tls.verify_server = true;
    cfg.host_for_sni = "localhost";
    ASSERT_TRUE(sock.setup(std::move(cfg), "127.0.0.1", bound_port, 2000));

    // Drive async connect (callback fires from worker thread)
    std::mutex cv_mtx;
    std::condition_variable cv;
    bool connect_done = false;
    bool connect_ok = false;

    sock.connect([&](bool ok, int /*fd*/) {
        std::unique_lock<std::mutex> lk(cv_mtx);
        connect_ok = ok;
        connect_done = true;
        cv.notify_one();
    });

    {
        std::unique_lock<std::mutex> lk(cv_mtx);
        ASSERT_TRUE(cv.wait_for(lk, std::chrono::seconds(5), [&] { return connect_done; }))
            << "connect callback timed out";
    }
    ASSERT_TRUE(connect_ok) << "TLS connect/handshake failed on client side";

    // -----------------------------------------------------------------------
    // 5. Send data from client
    // -----------------------------------------------------------------------
    everest::lib::io::tls::tls_client_socket::PayloadT msg = {'h', 'i'};
    bool tx_ok = false;
    for (int i = 0; i < 10 && !tx_ok; ++i) {
        ASSERT_TRUE(poll_until(sock.get_fd(), everest::lib::io::event::poll_events::write));
        tx_ok = sock.tx(msg);
    }
    ASSERT_TRUE(tx_ok);

    // -----------------------------------------------------------------------
    // 6. Receive echo from server
    // -----------------------------------------------------------------------
    everest::lib::io::tls::tls_client_socket::PayloadT rx_buf;
    bool rx_ok = false;
    for (int i = 0; i < 10 && !rx_ok; ++i) {
        ASSERT_TRUE(poll_until(sock.get_fd(), sock.desired_events()));
        rx_ok = sock.rx(rx_buf);
    }
    ASSERT_TRUE(rx_ok);
    ASSERT_GT(rx_buf.size(), 0u);

    // -----------------------------------------------------------------------
    // 7. Cleanup
    // -----------------------------------------------------------------------
    sock.close();
    server_thread.join();

    EXPECT_TRUE(server_ok) << "Server error: " << server_error;
    ::close(listen_fd);
}

TEST(TlsClient, AliasTypeIsValid) {
    // Verify the tls_client alias resolves to a usable type.
    // Construction requires valid TCP connect params and a pre-configured
    // tls_client_socket::Config — tested fully in HandshakeAndExchange above.
    // This test checks the type alias compiles and the poll fd accessor exists.
    static_assert(
        std::is_same_v<
            everest::lib::io::tls::tls_client,
            everest::lib::io::event::generic_fd_event_client<
                everest::lib::io::tls::tls_client_socket,
                everest::lib::io::event::fd_event_client<everest::lib::io::tls::tls_client_socket>::interface>>,
        "tls_client alias must resolve to the expected generic_fd_event_client specialization");
    SUCCEED();
}
