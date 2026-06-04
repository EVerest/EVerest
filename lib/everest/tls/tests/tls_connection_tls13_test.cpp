// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest

#include "tls_connection_test.hpp"

#include <arpa/inet.h>
#include <array>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/x509.h>
#include <optional>
#include <sys/socket.h>
#include <thread>

using namespace std::chrono_literals;

namespace {

TEST_F(TlsTest, EnforceTls13RejectsTls12Client) {
    // Server enforces TLS 1.3; a TLS-1.2-only client must be rejected.
    server_config.ciphersuites = "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384";
    server_config.enforce_tls_1_3 = true;

    // Force the client to negotiate TLS 1.2 only by clearing TLS 1.3 ciphersuites.
    client_config.ciphersuites = "";

    start();
    connect();
    EXPECT_FALSE(is_set(flags_t::connected));
}

TEST_F(TlsTest, EnforceTls13AcceptsTls13ClientWithAes256) {
    // Server enforces TLS 1.3; client offers TLS_AES_256_GCM_SHA384 and connects.
    server_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    server_config.enforce_tls_1_3 = true;

    client_config.ciphersuites = "TLS_AES_256_GCM_SHA384";

    start();
    connect();
    EXPECT_TRUE(is_set(flags_t::connected));
}

TEST_F(TlsTest, NoEnforceTls13EmptyCiphersuitesRejectsTls13Client) {
    // Existing behavior: with enforce_tls_1_3 = false and empty ciphersuites,
    // the server caps at TLS 1.2 and a TLS-1.3-only client cannot connect.
    server_config.ciphersuites = "";
    server_config.enforce_tls_1_3 = false;

    // Pin the client to TLS 1.3 to force a 1.3-only ClientHello so the
    // 1.2-cap on the server is the reason the handshake fails.
    client_config.cipher_list = nullptr;
    client_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    client_config.min_proto_version = TLS1_3_VERSION;

    start();
    connect();
    EXPECT_FALSE(is_set(flags_t::connected));
}

TEST_F(TlsTest, NoEnforceTls13NonEmptyCiphersuitesAcceptsBothVersions) {
    // With enforce_tls_1_3 = false and non-empty ciphersuites the server should
    // accept both TLS 1.2 and TLS 1.3 negotiations.
    server_config.ciphersuites = "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384";
    server_config.enforce_tls_1_3 = false;

    // TLS 1.2 client.
    client_config.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
    client_config.ciphersuites = "";

    start();
    connect();
    EXPECT_TRUE(is_set(flags_t::connected));

    // TLS 1.3 client against the same running server.
    client_config.cipher_list = nullptr;
    client_config.ciphersuites = "TLS_AES_256_GCM_SHA384";

    connect();
    EXPECT_TRUE(is_set(flags_t::connected));
}

TEST_F(TlsTest, Tls13ClientWithoutCertHandshakeFails) {
    // A TLS 1.3 client that presents no client certificate must be rejected
    // because the dispatcher upgrades verify mode to require a peer cert. In
    // TLS 1.3 the server's fatal alert (certificate_required) arrives after
    // the client's SSL_connect has returned, so we probe the connection with a
    // read to force the alert to surface and confirm the handshake failed.
    server_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    server_config.enforce_tls_1_3 = true;

    client_config.cipher_list = nullptr;
    client_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    client_config.min_proto_version = TLS1_3_VERSION;
    // No client certificate: certificate_chain_file / private_key_file remain null.

    start();
    bool handshake_ok{false};
    connect([&handshake_ok](tls::Client::ConnectionPtr& con) {
        if (!con) {
            return;
        }
        if (con->connect() != tls::Connection::result_t::success) {
            return;
        }
        // Surface any pending fatal alert from the server.
        std::byte buf[1]{};
        std::size_t got{0};
        const auto rc = con->read(buf, sizeof(buf), got, 200);
        handshake_ok =
            (rc != tls::Connection::result_t::closed) && (con->state() == tls::Connection::state_t::connected);
    });
    EXPECT_FALSE(handshake_ok);
}

TEST_F(TlsTest, Tls13ClientWithCertHandshakeSucceeds) {
    // A TLS 1.3 client offering a certificate chained to a trust anchor the
    // server has configured must complete the handshake. Setting
    // verify_client = true loads verify_locations_file up-front so the
    // dispatcher's TLS 1.3 verify upgrade is idempotent on this path.
    server_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    server_config.enforce_tls_1_3 = true;
    server_config.verify_client = true;
    server_config.verify_locations_file = "client_root_cert.pem";

    client_config.cipher_list = nullptr;
    client_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    client_config.min_proto_version = TLS1_3_VERSION;
    client_config.certificate_chain_file = "client_chain.pem";
    client_config.private_key_file = "client_priv.pem";

    start();
    connect();
    EXPECT_TRUE(is_set(flags_t::connected));
}

TEST_F(TlsTest, Tls12ClientWithoutCertHandshakeSucceeds) {
    // Pre-existing TLS 1.2 behavior must be preserved: a TLS 1.2 client with
    // no peer certificate connects when the server does not require one.
    // enforce_tls_1_3 is left at its default (false) and verify_client is
    // false, matching the legacy server configuration.
    server_config.ciphersuites = "";
    server_config.enforce_tls_1_3 = false;

    client_config.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
    client_config.ciphersuites = "";

    start();
    connect();
    EXPECT_TRUE(is_set(flags_t::connected));
}

TEST_F(TlsTest, PeerCertificateSha512Tls13WithCert) {
    // After a TLS 1.3 handshake with a client cert the server-side accessor
    // must return the SHA-512 digest of the peer's leaf certificate DER.
    server_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    server_config.enforce_tls_1_3 = true;
    server_config.verify_client = true;
    server_config.verify_locations_file = "client_root_cert.pem";

    client_config.cipher_list = nullptr;
    client_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    client_config.min_proto_version = TLS1_3_VERSION;
    client_config.certificate_chain_file = "client_chain.pem";
    client_config.private_key_file = "client_priv.pem";

    std::mutex result_mutex;
    std::condition_variable result_cv;
    bool handler_done{false};
    bool accept_ok{false};
    std::optional<std::array<std::uint8_t, 64>> server_digest;

    auto server_handler = [&](tls::Server::ConnectionPtr&& con) {
        if (con && con->accept() == tls::Connection::result_t::success) {
            accept_ok = true;
            server_digest = con->peer_certificate_sha512();
            (void)con->shutdown();
        }
        {
            std::lock_guard<std::mutex> lock(result_mutex);
            handler_done = true;
        }
        result_cv.notify_all();
    };

    start(server_handler);
    connect();
    EXPECT_TRUE(is_set(flags_t::connected));

    {
        std::unique_lock<std::mutex> lock(result_mutex);
        result_cv.wait_for(lock, 2s, [&] { return handler_done; });
    }
    ASSERT_TRUE(accept_ok);
    ASSERT_TRUE(server_digest.has_value());

    // Independently compute SHA-512 over the leaf certificate's DER encoding.
    auto leaf_certs = openssl::load_certificates("client_cert.pem");
    ASSERT_FALSE(leaf_certs.empty());
    auto der = openssl::certificate_to_der(leaf_certs.front().get());
    ASSERT_TRUE(static_cast<bool>(der));
    openssl::sha_512_digest_t expected{};
    ASSERT_TRUE(openssl::sha_512(der.get(), der.size(), expected));

    EXPECT_EQ(*server_digest, expected);
}

TEST_F(TlsTest, PeerCertificateSha512Tls12WithoutCert) {
    // TLS 1.2 path with no peer certificate: accessor must return nullopt.
    server_config.ciphersuites = "";
    server_config.enforce_tls_1_3 = false;
    server_config.verify_client = false;

    client_config.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
    client_config.ciphersuites = "";

    std::mutex result_mutex;
    std::condition_variable result_cv;
    bool handler_done{false};
    bool accept_ok{false};
    std::optional<std::array<std::uint8_t, 64>> server_digest;
    bool digest_set{false};

    auto server_handler = [&](tls::Server::ConnectionPtr&& con) {
        if (con && con->accept() == tls::Connection::result_t::success) {
            accept_ok = true;
            server_digest = con->peer_certificate_sha512();
            digest_set = true;
            (void)con->shutdown();
        }
        {
            std::lock_guard<std::mutex> lock(result_mutex);
            handler_done = true;
        }
        result_cv.notify_all();
    };

    start(server_handler);
    connect();
    EXPECT_TRUE(is_set(flags_t::connected));

    {
        std::unique_lock<std::mutex> lock(result_mutex);
        result_cv.wait_for(lock, 2s, [&] { return handler_done; });
    }
    ASSERT_TRUE(accept_ok);
    ASSERT_TRUE(digest_set);
    EXPECT_FALSE(server_digest.has_value());
}

TEST_F(TlsTest, WrapAcceptedFdHandshake) {
    // Drives the wrap_accepted_fd factory: the test owns the listen socket and
    // the accept(2) call, hands the resulting fd to the Server, and checks the
    // returned ConnectionPtr completes a TLS handshake against a client thread.
    using state_t = tls::Server::state_t;

    // Local listen socket on an ephemeral port. Created before init() so it
    // can be passed via server_config.socket; otherwise init_socket() would
    // bind the fixture's fixed port and leak that listener until process exit.
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

    // Bypass init_socket() by handing our test-owned listen fd to the Server.
    // This test never calls serve(); we only need init() to set up the SSL_CTX.
    server_config.socket = listen_fd;
    const auto init_state = server.init(server_config, nullptr);
    ASSERT_TRUE(init_state == state_t::init_complete || init_state == state_t::init_socket);

    sockaddr_in bound_addr{};
    socklen_t bound_len = sizeof(bound_addr);
    ASSERT_EQ(::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&bound_addr), &bound_len), 0);
    const std::uint16_t bound_port = ntohs(bound_addr.sin_port);
    const std::string bound_service = std::to_string(bound_port);

    // Client thread: open a TLS 1.2 connection to our listen socket.
    std::thread client_thread([&]() {
        ClientTest local_client;
        local_client.init(client_config);
        auto conn = local_client.connect("127.0.0.1", bound_service.c_str(), false, 1000);
        if (conn) {
            (void)conn->connect();
            (void)conn->shutdown();
        }
    });

    // Accept the incoming TCP connection on the test-owned socket.
    sockaddr_in peer_addr{};
    socklen_t peer_len = sizeof(peer_addr);
    const int accepted_fd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&peer_addr), &peer_len);
    ASSERT_GE(accepted_fd, 0);

    char ip_buf[INET_ADDRSTRLEN]{};
    char service_buf[NI_MAXSERV]{};
    ASSERT_EQ(::getnameinfo(reinterpret_cast<sockaddr*>(&peer_addr), peer_len, ip_buf, sizeof(ip_buf), service_buf,
                            sizeof(service_buf), NI_NUMERICHOST | NI_NUMERICSERV),
              0);

    // Hand the accepted fd to the factory under test.
    tls::Server::ConnectionPtr server_conn = server.wrap_accepted_fd(accepted_fd, ip_buf, service_buf);
    ASSERT_TRUE(server_conn);

    // Drive the SSL handshake.
    EXPECT_EQ(server_conn->accept(1000), tls::Connection::result_t::success);
    EXPECT_EQ(server_conn->state(), tls::Connection::state_t::connected);
    (void)server_conn->shutdown();

    if (client_thread.joinable()) {
        client_thread.join();
    }
    (void)::close(listen_fd);
}

TEST_F(TlsTest, LastErrorPopulatedOnFailedAccept) {
    // TLS 1.3 server requires a client cert chained to client_root_cert.pem.
    // The client presents an untrusted cert (alt_client_chain.pem), so the
    // server-side accept() fails certificate verification at the SSL layer and
    // last_error() must be populated with the queued OpenSSL error text.
    //
    // NOTE: this uses the fallback trigger from the task. The primary no-client-
    // cert path failed the server accept() via SSL_ERROR_SYSCALL (peer reset the
    // socket before the server could surface a clean alert), which leaves the
    // OpenSSL error queue empty. An untrusted client cert makes verification fail
    // during processing of the client's handshake flight, which deterministically
    // queues a "certificate verify failed" SSL error regardless of socket timing.
    server_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    server_config.enforce_tls_1_3 = true;
    server_config.verify_client = true;
    server_config.verify_locations_file = "client_root_cert.pem";

    client_config.cipher_list = nullptr;
    client_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    client_config.min_proto_version = TLS1_3_VERSION;
    // Untrusted client certificate (not chained to client_root_cert.pem).
    client_config.certificate_chain_file = "alt_client_chain.pem";
    client_config.private_key_file = "alt_client_priv.pem";

    std::mutex result_mutex;
    std::condition_variable result_cv;
    bool handler_done{false};
    bool accept_failed{false};
    std::string server_last_error;

    auto server_handler = [&](tls::Server::ConnectionPtr&& con) {
        if (con) {
            const auto rc = con->accept();
            accept_failed = (rc != tls::Connection::result_t::success);
            server_last_error = con->last_error();
            (void)con->shutdown();
        }
        {
            std::lock_guard<std::mutex> lock(result_mutex);
            handler_done = true;
        }
        result_cv.notify_all();
    };

    start(server_handler);
    connect();
    {
        std::unique_lock<std::mutex> lock(result_mutex);
        result_cv.wait_for(lock, 2s, [&] { return handler_done; });
    }
    ASSERT_TRUE(handler_done);
    EXPECT_TRUE(accept_failed);
    EXPECT_FALSE(server_last_error.empty());
}

} // namespace
