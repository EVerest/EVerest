// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <netinet/in.h>

#include <catch2/catch_test_macros.hpp>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/tls/tls_listener.hpp>
#include <everest/io/tls/tls_listener_config.hpp>
#include <everest/io/tls/tls_server.hpp>
#include <everest/io/tls/tls_server_socket.hpp>

#include <iso15118/ev/transport/data_client.hpp>
#include <iso15118/io/ipv6_endpoint.hpp>

using everest::lib::io::event::fd_event_handler;
using iso15118::ev::transport::DataClient;
using iso15118::ev::transport::TlsParams;
using iso15118::io::Ipv6EndPoint;

namespace io = everest::lib::io;

namespace {

// The PKI is generated into the build tree by the POST_BUILD step of this target.
std::string pki(const std::string& relative) {
    return std::string(PKI_PATH) + "/" + relative;
}

constexpr auto PKI_PASSWORD = "123456";
constexpr auto V2G_ROOT = "certs/ca/v2g/V2G_ROOT_CA.pem";
constexpr auto SECC_CHAIN = "certs/client/cso/CPO_CERT_CHAIN.pem";
constexpr auto SECC_CHAIN_NO_DC = "certs/client/cso/CPO_CERT_CHAIN_NO_DC.pem";
constexpr auto SECC_KEY = "certs/client/cso/SECC_LEAF.key";
constexpr auto SECC_KEY_NO_DC = "certs/client/cso/SECC_LEAF_NO_DC.key";
constexpr auto VEHICLE_CHAIN = "certs/ca/vehicle/VEHICLE_CERT_CHAIN.pem";
constexpr auto VEHICLE_KEY = "certs/client/vehicle/VEHICLE_LEAF.key";

// A SECC-side TLS listener on [::1]:0 that echoes whatever it receives.
struct SeccListener {
    SeccListener(fd_event_handler& ev, io::tls::tls_listener::Config cfg) : listener(std::move(cfg)) {
        listener.set_accept_callback(
            [this, &ev](std::unique_ptr<io::tls::tls_server> conn, std::string, std::uint16_t) {
                conn->set_rx_handler(
                    [](io::tls::tls_server_socket::PayloadT const& payload, auto& self) { self.tx(payload); });
                ev.register_event_handler(conn.get());
                peer = std::move(conn);
            });
        registered = ev.register_event_handler(&listener);
    }

    std::uint16_t port() const {
        return listener.listen_port();
    }

    io::tls::tls_listener listener;
    std::unique_ptr<io::tls::tls_server> peer;
    bool registered{false};
};

// chain/key select the presented SECC leaf; verify_client turns on mTLS (ISO 15118-20).
io::tls::tls_listener::Config secc_config(bool tls_1_3, const std::string& chain, const std::string& key,
                                          bool verify_client) {
    io::tls::tls_listener::Config cfg;
    cfg.bind_addr = "::1";
    cfg.ipv6_only = true;
    cfg.bind_port = 0;

    auto& certificates = cfg.tls.chains.emplace_back();
    certificates.certificate_chain_file = pki(chain);
    certificates.private_key_file = pki(key);
    certificates.private_key_password = PKI_PASSWORD;
    certificates.trust_anchor_file = pki(V2G_ROOT);

    cfg.tls.verify_locations_file = pki(V2G_ROOT);
    cfg.tls.verify_client = verify_client;
    cfg.tls.io_timeout_ms = 2000;

    if (tls_1_3) {
        cfg.tls.enforce_tls_1_3 = true;
        cfg.tls.ciphersuites = "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256";
    } else {
        cfg.tls.ciphersuites = "";
        cfg.tls.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
    }
    return cfg;
}

TlsParams iso2_params() {
    TlsParams params;
    params.tls_1_3 = false;
    params.verify_server = true;
    params.v2g_root_cert_path = pki(V2G_ROOT);
    return params;
}

TlsParams iso20_params() {
    TlsParams params;
    params.tls_1_3 = true;
    params.verify_server = true;
    params.v2g_root_cert_path = pki(V2G_ROOT);
    params.client_cert_chain_path = pki(VEHICLE_CHAIN);
    params.client_key_path = pki(VEHICLE_KEY);
    params.client_key_password = PKI_PASSWORD;
    return params;
}

Ipv6EndPoint loopback_endpoint(uint16_t port) {
    Ipv6EndPoint endpoint{};
    endpoint.port = port;
    std::memcpy(endpoint.address, &in6addr_loopback, sizeof(endpoint.address));
    return endpoint;
}

template <class Predicate> bool pump_until(fd_event_handler& ev, Predicate&& done, std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (not done() and std::chrono::steady_clock::now() < deadline) {
        ev.poll(std::chrono::milliseconds(10));
        ev.run_actions();
    }
    return done();
}

// Connects, waits for the handshake and round-trips one frame through the echo listener.
struct RoundTrip {
    int connected{0};
    int failed{0};
    std::vector<uint8_t> echo;
};

RoundTrip round_trip(fd_event_handler& ev, DataClient& client, SeccListener& secc, const TlsParams& params) {
    RoundTrip result;
    client.on_rx(
        [&](const std::vector<uint8_t>& bytes) { result.echo.insert(result.echo.end(), bytes.begin(), bytes.end()); });
    client.connect(
        loopback_endpoint(secc.port()), "", params, [&]() { ++result.connected; }, [&]() { ++result.failed; });

    pump_until(
        ev, [&]() { return result.connected > 0 or result.failed > 0; }, std::chrono::seconds(10));
    return result;
}

} // namespace

SCENARIO("ISO15118-20 EV DataClient completes a TLS 1.3 mTLS handshake and carries frames") {
    GIVEN("a SECC listener requiring a vehicle certificate") {
        fd_event_handler handler;
        SeccListener secc(handler, secc_config(true, SECC_CHAIN, SECC_KEY, true));
        REQUIRE(secc.registered);
        REQUIRE(secc.port() > 0);

        DataClient client(handler);

        WHEN("the EV connects with the ISO 15118-20 TLS profile") {
            auto result = round_trip(handler, client, secc, iso20_params());

            THEN("the handshake succeeds and a frame round-trips through the encrypted link") {
                REQUIRE(result.failed == 0);
                REQUIRE(result.connected == 1);

                const std::vector<uint8_t> frame{0x01, 0xfe, 0x80, 0x01, 0x00, 0x00, 0x00, 0x02, 0xde, 0xad};
                REQUIRE(client.send(frame));
                REQUIRE(pump_until(
                    handler, [&]() { return result.echo.size() >= frame.size(); }, std::chrono::seconds(5)));
                REQUIRE(result.echo == frame);
            }
        }
    }
}

SCENARIO("ISO15118-2 EV DataClient completes a TLS 1.2 handshake without a client certificate") {
    GIVEN("a SECC listener that does not request a client certificate") {
        fd_event_handler handler;
        SeccListener secc(handler, secc_config(false, SECC_CHAIN, SECC_KEY, false));
        REQUIRE(secc.registered);
        REQUIRE(secc.port() > 0);

        DataClient client(handler);

        WHEN("the EV connects with the ISO 15118-2 TLS profile") {
            auto result = round_trip(handler, client, secc, iso2_params());

            THEN("the handshake succeeds and a frame round-trips through the encrypted link") {
                REQUIRE(result.failed == 0);
                REQUIRE(result.connected == 1);

                const std::vector<uint8_t> frame{0x01, 0xfe, 0x80, 0x01, 0x00, 0x00, 0x00, 0x02, 0xbe, 0xef};
                REQUIRE(client.send(frame));
                REQUIRE(pump_until(
                    handler, [&]() { return result.echo.size() >= frame.size(); }, std::chrono::seconds(5)));
                REQUIRE(result.echo == frame);
            }
        }
    }
}

SCENARIO("ISO15118-2 EV DataClient rejects a SECC leaf without DomainComponent=CPO") {
    // [V2G2-875]: the chain verifies against the V2G root, only the CPO RDN is missing.
    GIVEN("a SECC listener presenting a leaf without the CPO DomainComponent") {
        fd_event_handler handler;
        SeccListener secc(handler, secc_config(false, SECC_CHAIN_NO_DC, SECC_KEY_NO_DC, false));
        REQUIRE(secc.registered);
        REQUIRE(secc.port() > 0);

        DataClient client(handler);

        WHEN("the EV connects with the ISO 15118-2 TLS profile") {
            auto result = round_trip(handler, client, secc, iso2_params());

            THEN("the connection is failed and never reported as connected") {
                REQUIRE(result.failed == 1);
                REQUIRE(result.connected == 0);

                // The deferred teardown must have dropped the client, so sends are refused.
                REQUIRE(pump_until(
                    handler, [&]() { return not client.send({0x01}); }, std::chrono::seconds(2)));
            }
        }
    }
}
