// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <iso15118/config.hpp>
#include <iso15118/io/connection_client_ssl.hpp>
#include <iso15118/io/connection_ssl.hpp>
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

config::SSLConfig secc_config() {
    config::SSLConfig config;
    config.backend = config::CertificateBackend::EVEREST_LAYOUT;
    auto& chain = config.chains.emplace_back();
    chain.path_certificate_chain = pki("certs/client/cso/CPO_CERT_CHAIN.pem");
    chain.path_certificate_key = pki("certs/client/cso/SECC_LEAF.key");
    chain.private_key_password = DEFAULT_PW;
    config.path_certificate_v2g_root = pki("certs/ca/v2g/V2G_ROOT_CA.pem");
    config.path_certificate_mo_root = pki("certs/ca/oem/OEM_ROOT_CA.pem");
    config.enforce_tls_1_3 = true;
    return config;
}

config::SSLConfig vehicle_config() {
    config::SSLConfig config;
    config.backend = config::CertificateBackend::EVEREST_LAYOUT;
    auto& chain = config.chains.emplace_back();
    chain.path_certificate_chain = pki("certs/ca/vehicle/VEHICLE_CERT_CHAIN.pem");
    chain.path_certificate_key = pki("certs/client/vehicle/VEHICLE_LEAF.key");
    chain.private_key_password = DEFAULT_PW;
    config.path_certificate_v2g_root = pki("certs/ca/v2g/V2G_ROOT_CA.pem");
    config.path_certificate_mo_root = pki("certs/ca/oem/OEM_ROOT_CA.pem");
    config.enforce_tls_1_3 = true;
    return config;
}

} // namespace

SCENARIO("TLS 1.3 connection between ConnectionSSL server and ConnectionClientSSL") {

    GIVEN("A ConnectionSSL server with the SECC leaf and a ConnectionClientSSL with the vehicle leaf") {
        io::PollManager poll_manager;
        io::ConnectionSSL server(poll_manager, INTERFACE, secc_config());

        std::vector<Event> server_events;
        std::vector<uint8_t> server_received;
        bool server_replied{false};

        server.set_event_callback([&](Event event) {
            server_events.push_back(event);
            if (event == Event::NEW_DATA) {
                std::array<uint8_t, 128> buffer{};
                const auto result = server.read(buffer.data(), buffer.size());
                server_received.insert(server_received.end(), buffer.begin(), buffer.begin() + result.bytes_read);
                if (not server_replied and result.bytes_read > 0) {
                    const std::array<uint8_t, 5> reply{'w', 'o', 'r', 'l', 'd'};
                    server.write(reply.data(), reply.size());
                    server_replied = true;
                }
            }
        });

        const auto endpoint = server.get_public_endpoint();

        WHEN("The client performs a mutual-auth handshake with server verification enabled") {
            io::ConnectionClientSSL client(poll_manager, INTERFACE, vehicle_config(), endpoint,
                                           /* verify_server_certificate */ true);

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
                    client_received.insert(client_received.end(), buffer.begin(), buffer.begin() + result.bytes_read);
                }
            });

            for (int i = 0; i < MAX_POLL_ITERATIONS; ++i) {
                if (server_received.size() >= 5 and client_received.size() >= 5) {
                    break;
                }
                poll_manager.poll(POLL_TIMEOUT_MS);
            }

            THEN("The handshake completes, events fire in order and application data flows") {
                REQUIRE(client_events.size() >= 3);
                REQUIRE(client_events[0] == Event::ACCEPTED);
                REQUIRE(client_events[1] == Event::OPEN);
                REQUIRE(client_events[2] == Event::NEW_DATA);

                REQUIRE(server_events.size() >= 3);
                REQUIRE(server_events[0] == Event::ACCEPTED);
                REQUIRE(server_events[1] == Event::OPEN);
                REQUIRE(server_events[2] == Event::NEW_DATA);

                const std::vector<uint8_t> expected_server_rx{'h', 'e', 'l', 'l', 'o'};
                const std::vector<uint8_t> expected_client_rx{'w', 'o', 'r', 'l', 'd'};
                REQUIRE(server_received == expected_server_rx);
                REQUIRE(client_received == expected_client_rx);
            }
        }
    }
}
