// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/ssl.h>

#include <iso15118/config.hpp>
#include <iso15118/io/connection_client_ssl.hpp>
#include <iso15118/io/ipv6_endpoint.hpp>
#include <iso15118/io/poll_manager.hpp>

using namespace iso15118;
using Event = io::ConnectionEvent;

namespace {
constexpr auto INTERFACE = "lo";
constexpr auto DEFAULT_PW = "123456";
constexpr int MAX_POLL_ITERATIONS = 400;
constexpr int POLL_TIMEOUT_MS = 20;

std::string pki(const std::string& relative) {
    return std::string(PKI_PATH) + "/" + relative;
}

int password_cb(char* buf, int size, int /* rwflag */, void* /* userdata */) {
    const std::string pw{DEFAULT_PW};
    const auto len = std::min(static_cast<int>(pw.size()), size);
    std::memcpy(buf, pw.c_str(), len);
    return len;
}

// A minimal blocking TLS 1.2-only server that presents the SECC leaf and does NOT request a client
// certificate (SSL_VERIFY_NONE). Used to exercise the ConnectionClientSSL TLS 1.2 path without a
// vehicle certificate.
struct RawTls12Server {
    int listen_fd{-1};
    uint16_t port{0};
    std::thread worker;
    std::atomic_bool client_cert_seen{false};
    std::atomic_bool handshake_ok{false};
    std::vector<uint8_t> received;

    RawTls12Server() {
        listen_fd = ::socket(AF_INET6, SOCK_STREAM, 0);
        REQUIRE(listen_fd >= 0);
        int reuse = 1;
        ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        sockaddr_in6 addr{};
        addr.sin6_family = AF_INET6;
        addr.sin6_addr = in6addr_loopback;
        addr.sin6_port = 0; // ephemeral
        REQUIRE(::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        REQUIRE(::listen(listen_fd, 1) == 0);

        sockaddr_in6 bound{};
        socklen_t len = sizeof(bound);
        REQUIRE(::getsockname(listen_fd, reinterpret_cast<sockaddr*>(&bound), &len) == 0);
        port = ntohs(bound.sin6_port);
    }

    void start() {
        worker = std::thread([this]() { this->run(); });
    }

    void run() {
        const int client_fd = ::accept(listen_fd, nullptr, nullptr);
        if (client_fd < 0) {
            return;
        }

        SSL_CTX* ctx = SSL_CTX_new(TLS_server_method());
        SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
        SSL_CTX_set_max_proto_version(ctx, TLS1_2_VERSION);
        SSL_CTX_set_cipher_list(ctx, "ECDHE-ECDSA-AES128-SHA256");
        SSL_CTX_set_default_passwd_cb(ctx, password_cb);
        SSL_CTX_use_certificate_chain_file(ctx, pki("certs/client/cso/CPO_CERT_CHAIN.pem").c_str());
        SSL_CTX_use_PrivateKey_file(ctx, pki("certs/client/cso/SECC_LEAF.key").c_str(), SSL_FILETYPE_PEM);
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);

        SSL* ssl = SSL_new(ctx);
        SSL_set_fd(ssl, client_fd);

        if (SSL_accept(ssl) == 1) {
            handshake_ok = true;
            client_cert_seen = (SSL_get_peer_certificate(ssl) != nullptr);

            std::array<uint8_t, 128> buffer{};
            const auto bytes = SSL_read(ssl, buffer.data(), buffer.size());
            if (bytes > 0) {
                received.insert(received.end(), buffer.begin(), buffer.begin() + bytes);
                const std::array<uint8_t, 5> reply{'w', 'o', 'r', 'l', 'd'};
                SSL_write(ssl, reply.data(), reply.size());
            }
        }

        SSL_shutdown(ssl);
        SSL_free(ssl);
        SSL_CTX_free(ctx);
        ::close(client_fd);
    }

    ~RawTls12Server() {
        if (worker.joinable()) {
            worker.join();
        }
        if (listen_fd >= 0) {
            ::close(listen_fd);
        }
    }
};

// Vehicle client config for TLS 1.2 with no client certificate (empty chain/key paths).
config::SSLConfig vehicle_config_no_cert() {
    config::SSLConfig config;
    config.backend = config::CertificateBackend::EVEREST_LAYOUT;
    config.path_certificate_chain = "";
    config.path_certificate_key = "";
    config.enforce_tls_1_3 = false;
    return config;
}

} // namespace

SCENARIO("ConnectionClientSSL completes a TLS 1.2 handshake without a client certificate") {

    GIVEN("A TLS 1.2-only server that does not request a client certificate") {
        RawTls12Server server;
        server.start();

        io::Ipv6EndPoint endpoint{};
        endpoint.port = server.port;
        endpoint.address[7] = htons(1); // ::1

        WHEN("The client connects with TLS 1.2 and no vehicle certificate configured") {
            io::PollManager poll_manager;
            io::ConnectionClientSSL client(poll_manager, INTERFACE, vehicle_config_no_cert(), endpoint,
                                           /* verify_server_certificate */ false);

            std::vector<Event> client_events;
            std::vector<uint8_t> client_received;

            client.set_event_callback([&](Event event) {
                client_events.push_back(event);
                if (event == Event::OPEN) {
                    const std::array<uint8_t, 5> hello{'h', 'e', 'l', 'l', 'o'};
                    client.write(hello.data(), hello.size());
                } else if (event == Event::NEW_DATA) {
                    std::array<uint8_t, 128> buffer{};
                    const auto result = client.read(buffer.data(), buffer.size());
                    client_received.insert(client_received.end(), buffer.begin(),
                                           buffer.begin() + result.bytes_read);
                }
            });

            for (int i = 0; i < MAX_POLL_ITERATIONS; ++i) {
                if (client_received.size() >= 5) {
                    break;
                }
                poll_manager.poll(POLL_TIMEOUT_MS);
            }

            THEN("The handshake succeeds, no client cert is presented and application data flows") {
                REQUIRE(server.handshake_ok.load());
                REQUIRE(not server.client_cert_seen.load());

                const std::vector<uint8_t> expected_server_rx{'h', 'e', 'l', 'l', 'o'};
                const std::vector<uint8_t> expected_client_rx{'w', 'o', 'r', 'l', 'd'};
                REQUIRE(server.received == expected_server_rx);
                REQUIRE(client_received == expected_client_rx);
            }
        }
    }
}
