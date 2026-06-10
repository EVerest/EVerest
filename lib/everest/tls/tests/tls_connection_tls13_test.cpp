// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include "tls_connection_test.hpp"

#include <array>
#include <condition_variable>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <openssl/x509.h>
#include <optional>
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

TEST_F(TlsTest, Tls13NoVerifyUpgradeAcceptsNoCert) {
    // With verify_client_on_tls13 left at its default (false) the server must NOT
    // upgrade verify mode for a TLS 1.3 connection. A client presenting no client
    // certificate must therefore still complete the handshake, preserving legacy
    // behavior for consumers that have not opted in to the upgrade.
    //
    // In TLS 1.3 a server's certificate_required alert arrives after the client's
    // SSL_connect returns, so (as in Tls13ClientWithoutCertHandshakeFails) we probe
    // the connection with a read: if the upgrade were active the read would observe
    // the alert and the connection would not be in the connected state.
    server_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    server_config.enforce_tls_1_3 = true;
    // verify_client stays at the fixture default (false); verify_client_on_tls13
    // stays at its new default (false).

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
    EXPECT_TRUE(handshake_ok);
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

TEST_F(TlsTest, EnforceTls13EmptyCiphersuitesFailsInit) {
    // enforce_tls_1_3 with an empty ciphersuites list would produce a server
    // that fails every handshake (TLS 1.3 minimum with no TLS 1.3 ciphersuite
    // available). init() must fail fast instead of reaching init_complete.
    using state_t = tls::Server::state_t;

    server_config.ciphersuites = "";
    server_config.enforce_tls_1_3 = true;

    // Hand a test-owned socket to the Server so init_socket() does not bind the
    // fixture's fixed port; this test never serves connections.
    const int test_socket = ::socket(AF_INET, SOCK_STREAM, 0);
    ASSERT_GE(test_socket, 0);
    server_config.socket = test_socket;

    const auto init_state = server.init(server_config, nullptr);
    EXPECT_NE(init_state, state_t::init_complete);
    (void)::close(test_socket);
}

TEST_F(TlsTest, AdditionalVerifyAnchorVerifiesClientChainedToIt) {
    // A client certificate chained to an anchor listed only in
    // verify_locations_additional_files must verify. Without the additional
    // anchors being loaded this client is rejected: LastErrorPopulatedOnFailedAccept
    // proves alt_client_chain fails against client_root_cert.pem alone.
    //
    // In TLS 1.3 the server's verification failure alert arrives after the
    // client's SSL_connect has returned, so probe the connection with a read
    // (as in Tls13ClientWithoutCertHandshakeFails) to surface any alert.
    server_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    server_config.enforce_tls_1_3 = true;
    server_config.verify_client = true;
    server_config.verify_locations_file = "client_root_cert.pem";
    server_config.verify_locations_additional_files = {tls::ConfigItem{"alt_client_root_cert.pem"}};

    client_config.cipher_list = nullptr;
    client_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    client_config.min_proto_version = TLS1_3_VERSION;
    client_config.certificate_chain_file = "alt_client_chain.pem";
    client_config.private_key_file = "alt_client_priv.pem";

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
    EXPECT_TRUE(handshake_ok);
}

TEST_F(TlsTest, AdditionalVerifyAnchorKeepsPrimaryAnchor) {
    // Additional anchors are loaded in addition to, not instead of, the primary
    // verify_locations_file: a client chained to the primary anchor must still
    // verify when additional anchors are configured.
    server_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    server_config.enforce_tls_1_3 = true;
    server_config.verify_client = true;
    server_config.verify_locations_file = "client_root_cert.pem";
    server_config.verify_locations_additional_files = {tls::ConfigItem{"alt_client_root_cert.pem"}};

    client_config.cipher_list = nullptr;
    client_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    client_config.min_proto_version = TLS1_3_VERSION;
    client_config.certificate_chain_file = "client_chain.pem";
    client_config.private_key_file = "client_priv.pem";

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
    EXPECT_TRUE(handshake_ok);
}

TEST_F(TlsTest, Tls13ClientWithoutCertHandshakeFails) {
    // A TLS 1.3 client that presents no client certificate must be rejected
    // because the dispatcher upgrades verify mode to require a peer cert. In
    // TLS 1.3 the server's fatal alert (certificate_required) arrives after
    // the client's SSL_connect has returned, so we probe the connection with a
    // read to force the alert to surface and confirm the handshake failed.
    server_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    server_config.enforce_tls_1_3 = true;
    server_config.verify_client_on_tls13 = true;

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
    const auto listener = make_loopback_listener();
    ASSERT_GE(listener.fd, 0);

    // Bypass init_socket() by handing our test-owned listen fd to the Server.
    // This test never calls serve(); we only need init() to set up the SSL_CTX.
    server_config.socket = listener.fd;
    const auto init_state = server.init(server_config, nullptr);
    ASSERT_TRUE(init_state == state_t::init_complete || init_state == state_t::init_socket);

    const std::string bound_service = std::to_string(listener.port);

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

    // Accept the incoming TCP connection on the test-owned socket and hand
    // the accepted fd to the factory under test.
    tls::Server::ConnectionPtr server_conn = accept_and_wrap(server, listener.fd);
    ASSERT_TRUE(server_conn);

    // Drive the SSL handshake.
    EXPECT_EQ(server_conn->accept(1000), tls::Connection::result_t::success);
    EXPECT_EQ(server_conn->state(), tls::Connection::state_t::connected);
    (void)server_conn->shutdown();

    if (client_thread.joinable()) {
        client_thread.join();
    }
    (void)::close(listener.fd);
}

TEST_F(TlsTest, WrapAcceptedFdOnUninitServerReturnsNull) {
    // A server whose init() failed or was never called has no SSL_CTX. Wrapping
    // an accepted fd must fail cleanly with nullptr rather than constructing a
    // connection over a null SSL (which would null-deref on a later accept()).
    // Per the documented ownership contract the factory does not close the fd on
    // failure, so the caller retains ownership and the fd stays open.
    int sv[2] = {-1, -1};
    ASSERT_EQ(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);

    // server is the fixture's default, uninitialised Server (start()/init() not called).
    tls::Server::ConnectionPtr conn = server.wrap_accepted_fd(sv[0], "127.0.0.1", "0");
    EXPECT_EQ(conn, nullptr);

    // The factory must not have closed the caller's fd.
    EXPECT_NE(::fcntl(sv[0], F_GETFD), -1);

    conn.reset();
    (void)::close(sv[0]);
    (void)::close(sv[1]);
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

TEST_F(TlsTest, LastErrorDoesNotBleedIntoGracefulClose) {
    // Two sequential server connections on this (the test) thread share the
    // process-thread-local OpenSSL error buffer behind last_error(). Connection
    // #1 fails certificate verification, seeding that buffer. Connection #2
    // completes a handshake and is then closed gracefully by the peer. After
    // the graceful close last_error() must be empty: the seed from #1 must not
    // bleed across the close. Server-side ops (accept/read/last_error) all run
    // here on the main thread, so the bleed (if present) is on this thread.
    using state_t = tls::Server::state_t;

    // Test-owned loopback listen socket on an ephemeral port (as in
    // WrapAcceptedFdHandshake), passed to the Server via server_config.socket so
    // init_socket() does not bind the fixture's fixed port.
    const auto listener = make_loopback_listener();
    ASSERT_GE(listener.fd, 0);

    // Server: TLS 1.3, require a client cert chained to client_root_cert.pem.
    server_config.socket = listener.fd;
    server_config.ciphersuites = "TLS_AES_256_GCM_SHA384";
    server_config.enforce_tls_1_3 = true;
    server_config.verify_client = true;
    server_config.verify_locations_file = "client_root_cert.pem";
    const auto init_state = server.init(server_config, nullptr);
    ASSERT_TRUE(init_state == state_t::init_complete || init_state == state_t::init_socket);

    const std::string bound_service = std::to_string(listener.port);

    // Spawn a TLS 1.3 client (on its own thread) presenting the given cert pair,
    // then gracefully shut down so the server observes a clean close_notify.
    auto spawn_client = [&](const char* chain, const char* key) {
        return std::thread([&, chain, key]() {
            ClientTest local_client;
            tls::Client::config_t cc = client_config;
            cc.cipher_list = nullptr;
            cc.ciphersuites = "TLS_AES_256_GCM_SHA384";
            cc.min_proto_version = TLS1_3_VERSION;
            cc.certificate_chain_file = chain;
            cc.private_key_file = key;
            local_client.init(cc);
            auto conn = local_client.connect("127.0.0.1", bound_service.c_str(), false, 1000);
            if (conn) {
                (void)conn->connect();
                (void)conn->shutdown();
            }
        });
    };

    // Connection #1: untrusted client cert -> accept() fails verification and
    // seeds the thread-local error buffer.
    std::thread client1 = spawn_client("alt_client_chain.pem", "alt_client_priv.pem");
    tls::Server::ConnectionPtr conn1 = accept_and_wrap(server, listener.fd);
    ASSERT_TRUE(conn1);
    EXPECT_NE(conn1->accept(1000), tls::Connection::result_t::success);
    EXPECT_FALSE(conn1->last_error().empty()); // seed confirmed
    if (client1.joinable()) {
        client1.join();
    }

    // Connection #2: trusted client cert -> handshake succeeds, peer closes
    // gracefully. The read must observe the graceful close, and last_error()
    // must then be empty (no bleed from connection #1).
    std::thread client2 = spawn_client("client_chain.pem", "client_priv.pem");
    tls::Server::ConnectionPtr conn2 = accept_and_wrap(server, listener.fd);
    ASSERT_TRUE(conn2);
    ASSERT_EQ(conn2->accept(1000), tls::Connection::result_t::success);

    std::byte buf[1]{};
    std::size_t got{0};
    const auto read_rc = conn2->read(buf, sizeof(buf), got, 1000);
    // Guard: the assertion below is only meaningful if the read actually saw the
    // graceful close. If it did not, fail loudly rather than silently passing.
    ASSERT_EQ(read_rc, tls::Connection::result_t::closed);
    EXPECT_TRUE(conn2->last_error().empty()) << "graceful close leaked a stale error: " << conn2->last_error();

    if (client2.joinable()) {
        client2.join();
    }
    (void)::close(listener.fd);
}

} // namespace
