// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <iso15118/config.hpp>
#include <iso15118/io/connection_ssl.hpp>
#include <iso15118/io/logging.hpp>
#include <iso15118/io/poll_manager.hpp>
#include <iso15118/io/sha_hash.hpp>

using namespace std::chrono_literals;

namespace {

constexpr auto LOOPBACK_IFACE = "lo";
constexpr auto SERVER_PORT = 50000;
constexpr auto DEFAULT_PW = "123456";
constexpr auto SECC_CHAIN = "pki/certs/client/cso/CPO_CERT_CHAIN.pem";
constexpr auto SECC_KEY = "pki/certs/client/cso/SECC_LEAF.key";
constexpr auto V2G_ROOT = "pki/certs/ca/v2g/V2G_ROOT_CA.pem";
constexpr auto OEM_ROOT = "pki/certs/ca/oem/OEM_ROOT_CA.pem";
constexpr auto VEHICLE_LEAF_PEM = "pki/certs/client/vehicle/VEHICLE_LEAF.pem";
constexpr auto VEHICLE_LEAF_KEY = "pki/certs/client/vehicle/VEHICLE_LEAF.key";
constexpr auto VEHICLE_CHAIN = "pki/certs/ca/vehicle/VEHICLE_CERT_CHAIN.pem";

// Without this the server thread can be killed by SIGPIPE when the synthetic client
// closes first and the server still has a NewSessionTicket queued.
struct IgnoreSigpipe {
    IgnoreSigpipe() {
        ::signal(SIGPIPE, SIG_IGN);
    }
};
const IgnoreSigpipe g_ignore_sigpipe{};

iso15118::config::SSLConfig make_ssl_config(bool enable_keylog, const std::filesystem::path& keylog_dir,
                                            bool enforce_tls_1_3) {
    std::vector<iso15118::config::ChainConfig> chains;
    chains.push_back(iso15118::config::ChainConfig{
        SECC_CHAIN,
        SECC_KEY,
        DEFAULT_PW,
        {},
    });
    return iso15118::config::SSLConfig{
        iso15118::config::CertificateBackend::EVEREST_LAYOUT,
        {},
        std::move(chains),
        V2G_ROOT,
        OEM_ROOT,
        false,
        enable_keylog,
        enforce_tls_1_3,
        keylog_dir,
    };
}

// Compute SHA-512 over the DER encoding of the leaf cert in a PEM file. Matches what
// X509_digest(peer, EVP_sha512(), ...) produces on the server side.
iso15118::io::sha512_hash_t sha512_of_pem_leaf(const std::string& pem_path) {
    auto bio = BIO_new_file(pem_path.c_str(), "r");
    REQUIRE(bio != nullptr);
    auto* x509 = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    REQUIRE(x509 != nullptr);

    iso15118::io::sha512_hash_t out{};
    unsigned int len = 0;
    const auto ok = X509_digest(x509, EVP_sha512(), out.data(), &len);
    X509_free(x509);
    REQUIRE(ok == 1);
    REQUIRE(len == out.size());
    return out;
}

struct ClientResult {
    bool handshake_ok{false};
    std::string error;
    bool tls_1_3{false};
    std::string read_payload;
};

// Drive a synthetic OpenSSL client over TCP/IPv6 loopback. Optionally present a client cert.
ClientResult run_tls_client(const std::string& send_payload, std::size_t expect_recv, bool present_client_cert,
                            bool enforce_tls_1_3, bool force_tls_1_2 = false) {
    ClientResult result;

    // Resolve [::1] manually
    sockaddr_in6 addr{};
    addr.sin6_family = AF_INET6;
    addr.sin6_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET6, "::1", &addr.sin6_addr) != 1) {
        result.error = "inet_pton failed";
        return result;
    }

    int fd = ::socket(AF_INET6, SOCK_STREAM, 0);
    if (fd < 0) {
        result.error = "socket() failed";
        return result;
    }

    // Retry connect a few times to allow the server to start listening
    int connected = -1;
    for (int i = 0; i < 50; ++i) {
        connected = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        if (connected == 0) {
            break;
        }
        std::this_thread::sleep_for(20ms);
    }
    if (connected != 0) {
        ::close(fd);
        result.error = "connect() failed";
        return result;
    }

    auto* method = TLS_client_method();
    auto* ctx = SSL_CTX_new(method);
    if (ctx == nullptr) {
        ::close(fd);
        result.error = "SSL_CTX_new failed";
        return result;
    }
    if (enforce_tls_1_3) {
        SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
    } else if (force_tls_1_2) {
        SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
        SSL_CTX_set_max_proto_version(ctx, TLS1_2_VERSION);
    } else {
        SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
    }
    SSL_CTX_set_ciphersuites(ctx, "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256");
    SSL_CTX_set_cipher_list(ctx, "ECDHE-ECDSA-AES128-SHA256");
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);

    if (present_client_cert) {
        // Use vehicle leaf as the client cert. SECC server side has SSL_VERIFY_NONE so
        // any cert (or none) is accepted — we only need the cert presented to drive the
        // SHA-512 path on the server.
        if (SSL_CTX_use_certificate_chain_file(ctx, VEHICLE_CHAIN) != 1) {
            SSL_CTX_free(ctx);
            ::close(fd);
            result.error = "client use_certificate_chain_file failed";
            return result;
        }
        // VEHICLE_LEAF.key is encrypted with DEFAULT_PW
        SSL_CTX_set_default_passwd_cb_userdata(ctx, const_cast<char*>(DEFAULT_PW));
        SSL_CTX_set_default_passwd_cb(ctx, [](char* buf, int size, int, void* ud) -> int {
            const auto* pw = static_cast<const char*>(ud);
            const auto len = static_cast<int>(strlen(pw));
            const auto cp = (len < size) ? len : size;
            memcpy(buf, pw, cp);
            return cp;
        });
        if (SSL_CTX_use_PrivateKey_file(ctx, VEHICLE_LEAF_KEY, SSL_FILETYPE_PEM) != 1) {
            SSL_CTX_free(ctx);
            ::close(fd);
            result.error = "client use_PrivateKey_file failed";
            return result;
        }
    }

    auto* ssl = SSL_new(ctx);
    if (ssl == nullptr) {
        SSL_CTX_free(ctx);
        ::close(fd);
        result.error = "SSL_new failed";
        return result;
    }
    SSL_set_fd(ssl, fd);

    const auto handshake = SSL_connect(ssl);
    if (handshake != 1) {
        const auto err = SSL_get_error(ssl, handshake);
        result.error = "SSL_connect failed: " + std::to_string(err);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        ::close(fd);
        return result;
    }

    result.handshake_ok = true;
    result.tls_1_3 = (SSL_version(ssl) == TLS1_3_VERSION);

    // Send a payload
    if (not send_payload.empty()) {
        const auto written = SSL_write(ssl, send_payload.data(), static_cast<int>(send_payload.size()));
        if (written <= 0) {
            result.error = "SSL_write failed";
        }
    }

    // Read the echoed payload, if any expected
    if (expect_recv > 0) {
        std::vector<char> buf(expect_recv);
        std::size_t total = 0;
        while (total < expect_recv) {
            const auto n = SSL_read(ssl, buf.data() + total, static_cast<int>(expect_recv - total));
            if (n <= 0) {
                break;
            }
            total += static_cast<std::size_t>(n);
        }
        result.read_payload.assign(buf.data(), total);
    }

    SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    ::close(fd);
    return result;
}

// Drive poll_manager until predicate returns true or deadline expires. Used by the
// main test thread while a client thread runs concurrently.
template <typename Predicate>
bool poll_until(iso15118::io::PollManager& pm, Predicate done, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        pm.poll(50);
        if (done()) {
            return true;
        }
    }
    return false;
}

} // namespace

SCENARIO("ConnectionSSL completes a TLS handshake against a real client") {

    GIVEN("A ConnectionSSL configured with a single SECC chain") {
        iso15118::io::set_logging_callback([](iso15118::LogLevel, const std::string&) {});

        iso15118::io::PollManager poll_manager;
        const auto ssl_cfg = make_ssl_config(false, "/tmp", false);
        iso15118::io::ConnectionSSL connection(poll_manager, LOOPBACK_IFACE, ssl_cfg);

        std::atomic<bool> handshake_open{false};
        std::atomic<bool> got_new_data{false};
        std::string server_received;
        connection.set_event_callback([&](iso15118::io::ConnectionEvent event) {
            if (event == iso15118::io::ConnectionEvent::OPEN) {
                handshake_open.store(true);
            } else if (event == iso15118::io::ConnectionEvent::NEW_DATA) {
                std::array<uint8_t, 64> buf{};
                const auto r = connection.read(buf.data(), buf.size());
                if (r.bytes_read > 0) {
                    server_received.append(reinterpret_cast<const char*>(buf.data()), r.bytes_read);
                    got_new_data.store(true);
                }
            }
        });

        WHEN("A synthetic TLS client connects and exchanges bytes") {
            const std::string payload = "ping";
            auto client_future =
                std::async(std::launch::async, [&]() { return run_tls_client(payload, 0, false, false, true); });

            const bool got_open = poll_until(
                poll_manager, [&]() { return handshake_open.load(); }, 5s);

            // Drain server-side socket reads that arrive after handshake
            const bool got_data = poll_until(
                poll_manager, [&]() { return got_new_data.load(); }, 2s);

            const auto client_result = client_future.get();

            THEN("The handshake completes on both sides and a round-trip read works") {
                REQUIRE(client_result.error.empty());
                REQUIRE(client_result.handshake_ok);
                REQUIRE(got_open);
                REQUIRE(got_data);
                REQUIRE(server_received == payload);
            }
        }

        connection.close();
    }
}

SCENARIO("ConnectionSSL exposes the peer certificate SHA-512 to callers") {

    GIVEN("A ConnectionSSL configured with a single SECC chain and TLS 1.3 enforced") {
        iso15118::io::set_logging_callback([](iso15118::LogLevel, const std::string&) {});

        iso15118::io::PollManager poll_manager;
        const auto ssl_cfg = make_ssl_config(false, "/tmp", true);
        iso15118::io::ConnectionSSL connection(poll_manager, LOOPBACK_IFACE, ssl_cfg);

        std::atomic<bool> handshake_open{false};
        connection.set_event_callback([&](iso15118::io::ConnectionEvent event) {
            if (event == iso15118::io::ConnectionEvent::OPEN) {
                handshake_open.store(true);
            }
        });

        WHEN("A TLS 1.3 client presents the vehicle leaf certificate") {
            auto client_future = std::async(std::launch::async, [&]() { return run_tls_client({}, 0, true, true); });

            const bool got_open = poll_until(
                poll_manager, [&]() { return handshake_open.load(); }, 5s);

            poll_manager.poll(100);

            const auto client_result = client_future.get();

            THEN("get_vehicle_cert_hash() returns SHA-512 of the vehicle leaf") {
                REQUIRE(client_result.error.empty());
                REQUIRE(client_result.handshake_ok);
                REQUIRE(got_open);

                const auto expected = sha512_of_pem_leaf(VEHICLE_LEAF_PEM);
                const auto observed = connection.get_vehicle_cert_hash();
                REQUIRE(observed.has_value());
                REQUIRE(*observed == expected);
            }
        }

        connection.close();
    }
}

SCENARIO("ConnectionSSL writes an SSLKEYLOGFILE-format keylog when enabled") {

    GIVEN("A ConnectionSSL configured with key logging enabled and a writable keylog dir") {
        iso15118::io::set_logging_callback([](iso15118::LogLevel, const std::string&) {});

        const auto keylog_dir = std::filesystem::temp_directory_path() /
                                ("iso15118_keylog_" + std::to_string(::getpid()) + "_" +
                                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(keylog_dir);
        const auto keylog_file = keylog_dir / "tls_session_keys.log";
        if (std::filesystem::exists(keylog_file)) {
            std::filesystem::remove(keylog_file);
        }

        iso15118::io::PollManager poll_manager;
        const auto ssl_cfg = make_ssl_config(true, keylog_dir, false);
        iso15118::io::ConnectionSSL connection(poll_manager, LOOPBACK_IFACE, ssl_cfg);

        std::atomic<bool> handshake_open{false};
        connection.set_event_callback([&](iso15118::io::ConnectionEvent event) {
            if (event == iso15118::io::ConnectionEvent::OPEN) {
                handshake_open.store(true);
            }
        });

        WHEN("A TLS 1.2 client completes the handshake") {
            auto client_future =
                std::async(std::launch::async, [&]() { return run_tls_client({}, 0, false, false, true); });

            const bool got_open = poll_until(
                poll_manager, [&]() { return handshake_open.load(); }, 5s);

            poll_manager.poll(100);

            const auto client_result = client_future.get();

            THEN("The keylog file is created and contains an SSLKEYLOGFILE-format line") {
                REQUIRE(client_result.error.empty());
                REQUIRE(client_result.handshake_ok);
                REQUIRE(got_open);

                REQUIRE(std::filesystem::exists(keylog_file));
                std::ifstream ifs(keylog_file);
                std::string contents((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
                const auto has_client_random = contents.find("CLIENT_RANDOM ") != std::string::npos;
                const auto has_handshake_secret =
                    contents.find("CLIENT_HANDSHAKE_TRAFFIC_SECRET ") != std::string::npos;
                REQUIRE((has_client_random or has_handshake_secret));
            }
        }

        connection.close();
        std::error_code ec;
        std::filesystem::remove_all(keylog_dir, ec);
    }
}
