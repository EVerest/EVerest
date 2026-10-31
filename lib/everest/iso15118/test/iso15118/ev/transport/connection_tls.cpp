// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <catch2/catch_test_macros.hpp>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/tls/tls_listener.hpp>
#include <everest/io/tls/tls_listener_config.hpp>
#include <everest/io/tls/tls_server.hpp>
#include <everest/io/tls/tls_server_socket.hpp>

#include <iso15118/ev/controller.hpp>
#include <iso15118/ev/transport/connection.hpp>
#include <iso15118/io/ipv6_endpoint.hpp>
#include <iso15118/io/logging.hpp>
#include <iso15118/io/sdp_packet.hpp>
#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/message/variant.hpp>
#include <iso15118/session/protocol.hpp>

using everest::lib::io::event::fd_event_handler;
using iso15118::ev::config::TlsVersion;
using iso15118::ev::transport::Connection;
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

// A SECC-side TLS listener on [::1]:0 that records and echoes whatever it receives.
struct SeccListener {
    SeccListener(fd_event_handler& ev, io::tls::tls_listener::Config cfg) : listener(std::move(cfg)) {
        listener.set_accept_callback(
            [this, &ev](std::unique_ptr<io::tls::tls_server> conn, std::string, std::uint16_t) {
                conn->set_rx_handler([this](io::tls::tls_server_socket::PayloadT const& payload, auto& self) {
                    received.insert(received.end(), payload.begin(), payload.end());
                    self.tx(payload);
                });
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
    std::vector<uint8_t> received;
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

// A SECC that follows the EV's offer, 1.2 or 1.3. verify_client_on_tls13 requires the vehicle
// certificate once 1.3 is negotiated, as ISO 15118-20 mandates.
io::tls::tls_listener::Config any_version_secc_config(bool verify_client_on_tls13) {
    auto cfg = secc_config(false, SECC_CHAIN, SECC_KEY, false);
    cfg.tls.ciphersuites = "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256";
    cfg.tls.verify_client_on_tls13 = verify_client_on_tls13;
    return cfg;
}

TlsParams iso2_params() {
    TlsParams params;
    params.version = TlsVersion::Tls1_2;
    params.verify_server = true;
    params.v2g_root_cert_path = pki(V2G_ROOT);
    return params;
}

TlsParams iso20_params() {
    TlsParams params;
    params.version = TlsVersion::Tls1_3;
    params.verify_server = true;
    params.v2g_root_cert_path = pki(V2G_ROOT);
    params.client_cert_chain_path = pki(VEHICLE_CHAIN);
    params.client_key_path = pki(VEHICLE_KEY);
    params.client_key_password = PKI_PASSWORD;
    return params;
}

TlsParams auto_params() {
    auto params = iso20_params();
    params.version = TlsVersion::Auto;
    return params;
}

// Every message of one level logged while it lives. The Controller logs from its own thread.
struct LogCapture {
    explicit LogCapture(iso15118::LogLevel level_ = iso15118::LogLevel::Warning) : level(level_) {
        iso15118::io::set_logging_callback([this](iso15118::LogLevel logged, const std::string& message) {
            if (logged == level) {
                const std::lock_guard<std::mutex> lock(mutex);
                messages.push_back(message);
            }
        });
    }
    ~LogCapture() {
        iso15118::io::set_logging_callback([](iso15118::LogLevel, const std::string&) {});
    }
    int count(const std::string& text) {
        const std::lock_guard<std::mutex> lock(mutex);
        return static_cast<int>(std::count_if(messages.begin(), messages.end(), [&](const std::string& message) {
            return message.find(text) != std::string::npos;
        }));
    }
    bool contains(const std::string& text) {
        return count(text) > 0;
    }
    iso15118::LogLevel level;
    std::mutex mutex;
    std::vector<std::string> messages;
};

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

RoundTrip round_trip(fd_event_handler& ev, Connection& client, SeccListener& secc, const TlsParams& params) {
    RoundTrip result;
    client.on_rx(
        [&](const std::vector<uint8_t>& bytes) { result.echo.insert(result.echo.end(), bytes.begin(), bytes.end()); });
    client.connect(
        loopback_endpoint(secc.port()), "", params, [&]() { ++result.connected; }, [&]() { ++result.failed; });

    pump_until(
        ev, [&]() { return result.connected > 0 or result.failed > 0; }, std::chrono::seconds(10));
    return result;
}

// Runs Controller::loop on its own thread for the scope.
class ControllerRun {
public:
    explicit ControllerRun(iso15118::ev::Controller& controller_) :
        controller(controller_), worker([&controller_]() { controller_.loop(); }) {
    }
    ~ControllerRun() {
        controller.shutdown();
        worker.join();
    }
    ControllerRun(const ControllerRun&) = delete;
    ControllerRun& operator=(const ControllerRun&) = delete;

private:
    iso15118::ev::Controller& controller;
    std::thread worker;
};

// A Controller connecting straight to the SECC at @p port over TLS, tls require and version auto.
iso15118::ev::config::EvConfig auto_tls_controller_config(uint16_t port) {
    iso15118::ev::config::EvConfig config{};
    config.interface_name = "lo";
    config.evcc_id = "02:00:00:00:00:01";
    config.enable_sdp = false;
    config.direct_secc_endpoint = loopback_endpoint(port);
    config.direct_security = iso15118::v2gtp::Security::TLS;
    config.tls.mode = iso15118::ev::config::TlsMode::Require;
    config.tls.version = TlsVersion::Auto;
    config.tls.v2g_root_cert_path = pki(V2G_ROOT);
    config.tls.client_cert_chain_path = pki(VEHICLE_CHAIN);
    config.tls.client_key_path = pki(VEHICLE_KEY);
    config.tls.client_key_password = PKI_PASSWORD;
    return config;
}

// The first complete V2GTP frame in @p bytes, decoded as a request.
std::optional<iso15118::message_20::RequestVariant> first_request(const std::vector<uint8_t>& bytes) {
    constexpr auto header_size = iso15118::io::SdpPacket::V2GTP_HEADER_SIZE;
    if (bytes.size() < header_size) {
        return std::nullopt;
    }
    uint16_t type_be{};
    uint32_t length_be{};
    std::memcpy(&type_be, bytes.data() + 2, sizeof(type_be));
    std::memcpy(&length_be, bytes.data() + 4, sizeof(length_be));
    if (bytes.size() < header_size + ntohl(length_be)) {
        return std::nullopt;
    }
    return iso15118::message_20::RequestVariant{
        static_cast<iso15118::v2gtp::PayloadType>(ntohs(type_be)),
        iso15118::io::StreamInputView{bytes.data() + header_size, ntohl(length_be)}};
}

} // namespace

SCENARIO("ISO15118-20 EV Connection completes a TLS 1.3 mTLS handshake and carries frames") {
    GIVEN("a SECC listener requiring a vehicle certificate") {
        fd_event_handler handler;
        SeccListener secc(handler, secc_config(true, SECC_CHAIN, SECC_KEY, true));
        REQUIRE(secc.registered);
        REQUIRE(secc.port() > 0);

        Connection client(handler);

        WHEN("the EV connects with the ISO 15118-20 TLS profile") {
            auto result = round_trip(handler, client, secc, iso20_params());

            THEN("the handshake succeeds and a frame round-trips through the encrypted link") {
                REQUIRE(result.failed == 0);
                REQUIRE(result.connected == 1);
                REQUIRE(client.negotiated_tls_version() == TlsVersion::Tls1_3);

                const std::vector<uint8_t> frame{0x01, 0xfe, 0x80, 0x01, 0x00, 0x00, 0x00, 0x02, 0xde, 0xad};
                REQUIRE(client.send(frame));
                REQUIRE(pump_until(
                    handler, [&]() { return result.echo.size() >= frame.size(); }, std::chrono::seconds(5)));
                REQUIRE(result.echo == frame);
            }
        }
    }
}

SCENARIO("ISO15118-2 EV Connection completes a TLS 1.2 handshake without a client certificate") {
    GIVEN("a SECC listener that does not request a client certificate") {
        fd_event_handler handler;
        SeccListener secc(handler, secc_config(false, SECC_CHAIN, SECC_KEY, false));
        REQUIRE(secc.registered);
        REQUIRE(secc.port() > 0);

        Connection client(handler);

        WHEN("the EV connects with the ISO 15118-2 TLS profile") {
            auto result = round_trip(handler, client, secc, iso2_params());

            THEN("the handshake succeeds and a frame round-trips through the encrypted link") {
                REQUIRE(result.failed == 0);
                REQUIRE(result.connected == 1);
                REQUIRE(client.negotiated_tls_version() == TlsVersion::Tls1_2);

                const std::vector<uint8_t> frame{0x01, 0xfe, 0x80, 0x01, 0x00, 0x00, 0x00, 0x02, 0xbe, 0xef};
                REQUIRE(client.send(frame));
                REQUIRE(pump_until(
                    handler, [&]() { return result.echo.size() >= frame.size(); }, std::chrono::seconds(5)));
                REQUIRE(result.echo == frame);
            }
        }
    }
}

SCENARIO("ISO15118-2 EV Connection rejects a SECC leaf without DomainComponent=CPO") {
    // [V2G2-875]: the chain verifies against the V2G root, only the CPO RDN is missing.
    GIVEN("a SECC listener presenting a leaf without the CPO DomainComponent") {
        fd_event_handler handler;
        SeccListener secc(handler, secc_config(false, SECC_CHAIN_NO_DC, SECC_KEY_NO_DC, false));
        REQUIRE(secc.registered);
        REQUIRE(secc.port() > 0);

        Connection client(handler);

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

SCENARIO("EV Connection under tls_version auto negotiates what the SECC allows") {
    fd_event_handler handler;
    Connection client(handler);

    GIVEN("a TLS 1.3 SECC requiring a vehicle certificate") {
        SeccListener secc(handler, secc_config(true, SECC_CHAIN, SECC_KEY, true));
        REQUIRE(secc.registered);

        THEN("the EV presents its vehicle chain and TLS 1.3 is negotiated") {
            const auto result = round_trip(handler, client, secc, auto_params());
            REQUIRE(result.failed == 0);
            REQUIRE(result.connected == 1);
            REQUIRE(client.negotiated_tls_version() == TlsVersion::Tls1_3);
        }
    }

    GIVEN("a SECC that requires the vehicle certificate once TLS 1.3 is negotiated") {
        SeccListener secc(handler, any_version_secc_config(true));
        REQUIRE(secc.registered);

        THEN("TLS 1.3 is negotiated with the vehicle certificate") {
            const auto result = round_trip(handler, client, secc, auto_params());
            REQUIRE(result.failed == 0);
            REQUIRE(result.connected == 1);
            REQUIRE(client.negotiated_tls_version() == TlsVersion::Tls1_3);
        }
    }

    GIVEN("a TLS 1.2 SECC") {
        SeccListener secc(handler, secc_config(false, SECC_CHAIN, SECC_KEY, false));
        REQUIRE(secc.registered);

        THEN("TLS 1.2 is negotiated") {
            const auto result = round_trip(handler, client, secc, auto_params());
            REQUIRE(result.failed == 0);
            REQUIRE(result.connected == 1);
            REQUIRE(client.negotiated_tls_version() == TlsVersion::Tls1_2);
        }
    }

    GIVEN("a TLS 1.2 SECC presenting a leaf without DomainComponent=CPO") {
        SeccListener secc(handler, secc_config(false, SECC_CHAIN_NO_DC, SECC_KEY_NO_DC, false));
        REQUIRE(secc.registered);

        THEN("the negotiated ISO 15118-2 profile runs the [V2G2-875] check and fails the connection") {
            const auto result = round_trip(handler, client, secc, auto_params());
            REQUIRE(result.failed == 1);
            REQUIRE(result.connected == 0);
        }
    }
}

SCENARIO("EV Connection under tls_version auto caps at TLS 1.2 without a usable vehicle chain") {
    fd_event_handler handler;
    // Without the cap this SECC would accept TLS 1.3 without a client certificate.
    SeccListener secc(handler, any_version_secc_config(false));
    REQUIRE(secc.registered);
    Connection client(handler);
    LogCapture capture;

    GIVEN("no vehicle chain configured") {
        auto params = auto_params();
        params.client_cert_chain_path.clear();
        params.client_key_path.clear();

        THEN("TLS 1.2 is negotiated and the dropped TLS 1.3 is logged with its reason") {
            const auto result = round_trip(handler, client, secc, params);
            REQUIRE(result.connected == 1);
            REQUIRE(client.negotiated_tls_version() == TlsVersion::Tls1_2);
            REQUIRE(capture.contains("TLS 1.3 dropped: no vehicle certificate chain or key configured"));
        }
    }

    GIVEN("a vehicle chain file that does not exist") {
        auto params = auto_params();
        params.client_cert_chain_path = pki("certs/ca/vehicle/MISSING.pem");

        THEN("TLS 1.2 is negotiated and the missing file is named") {
            const auto result = round_trip(handler, client, secc, params);
            REQUIRE(result.connected == 1);
            REQUIRE(client.negotiated_tls_version() == TlsVersion::Tls1_2);
            REQUIRE(capture.contains("MISSING.pem is missing or unreadable"));
        }
    }

    GIVEN("a vehicle key file that does not exist") {
        auto params = auto_params();
        params.client_key_path = pki("certs/client/vehicle/MISSING.key");

        THEN("TLS 1.2 is negotiated and the missing key is named") {
            const auto result = round_trip(handler, client, secc, params);
            REQUIRE(result.connected == 1);
            REQUIRE(client.negotiated_tls_version() == TlsVersion::Tls1_2);
            REQUIRE(capture.contains("MISSING.key is missing or unreadable"));
        }
    }

    GIVEN("a vehicle key file that holds no private key") {
        auto params = auto_params();
        params.client_key_path = pki(VEHICLE_CHAIN);

        THEN("TLS 1.2 is negotiated and the undecodable key is named") {
            const auto result = round_trip(handler, client, secc, params);
            REQUIRE(result.connected == 1);
            REQUIRE(client.negotiated_tls_version() == TlsVersion::Tls1_2);
            REQUIRE(capture.contains("VEHICLE_CERT_CHAIN.pem cannot be decoded or its password is wrong"));
        }
    }

    GIVEN("a wrong vehicle key password") {
        auto params = auto_params();
        params.client_key_password = "wrong";

        THEN("TLS 1.2 is negotiated and the undecodable key is named") {
            const auto result = round_trip(handler, client, secc, params);
            REQUIRE(result.connected == 1);
            REQUIRE(client.negotiated_tls_version() == TlsVersion::Tls1_2);
            REQUIRE(capture.contains("VEHICLE_LEAF.key cannot be decoded or its password is wrong"));
        }
    }

    GIVEN("a vehicle key that does not match the vehicle leaf") {
        auto params = auto_params();
        params.client_key_path = pki(SECC_KEY);

        THEN("TLS 1.2 is negotiated and the mismatch is named") {
            const auto result = round_trip(handler, client, secc, params);
            REQUIRE(result.connected == 1);
            REQUIRE(client.negotiated_tls_version() == TlsVersion::Tls1_2);
            REQUIRE(capture.contains("SECC_LEAF.key does not match the vehicle leaf certificate"));
        }
    }
}

SCENARIO("EV Connection with tls_version pinned to 1.3 offers TLS 1.3 without a vehicle chain") {
    fd_event_handler handler;
    SeccListener secc(handler, any_version_secc_config(false));
    REQUIRE(secc.registered);
    Connection client(handler);

    auto params = iso20_params();
    params.client_cert_chain_path.clear();
    params.client_key_path.clear();

    const auto result = round_trip(handler, client, secc, params);
    REQUIRE(result.connected == 1);
    REQUIRE(client.negotiated_tls_version() == TlsVersion::Tls1_3);
}

SCENARIO("EV Connection with tls_version pinned to 1.3 fails on a vehicle key that does not match") {
    fd_event_handler handler;
    SeccListener secc(handler, any_version_secc_config(false));
    REQUIRE(secc.registered);
    Connection client(handler);

    auto params = iso20_params();
    params.client_key_path = pki(SECC_KEY);

    const auto result = round_trip(handler, client, secc, params);
    REQUIRE(result.failed == 1);
    REQUIRE(result.connected == 0);
}

SCENARIO("EV Controller under tls_version auto offers only what the negotiated TLS version carries") {
    using iso15118::ProtocolId;

    fd_event_handler handler;
    SeccListener secc(handler, secc_config(false, SECC_CHAIN, SECC_KEY, false));
    REQUIRE(secc.registered);
    auto config = auto_tls_controller_config(secc.port());
    std::atomic_bool stopped{false};
    iso15118::ev::feedback::Callbacks callbacks{};
    callbacks.stopped = [&stopped]() { stopped = true; };

    GIVEN("-20 and -2 enabled and a SECC that negotiates TLS 1.2") {
        config.supported_protocols = {ProtocolId::ISO15118_20, ProtocolId::ISO15118_2};
        iso15118::ev::Controller controller{config, callbacks};
        ControllerRun run{controller};

        THEN("the SupportedAppProtocolReq offers -2 alone") {
            REQUIRE(pump_until(
                handler, [&]() { return first_request(secc.received).has_value(); }, std::chrono::seconds(10)));
            const auto variant = first_request(secc.received);
            const auto* request = variant->get_if<iso15118::message_20::SupportedAppProtocolRequest>();
            REQUIRE(request != nullptr);
            REQUIRE(request->app_protocol.size() == 1);
            REQUIRE(request->app_protocol[0].protocol_namespace == iso15118::ISO2_NAMESPACE);
        }
    }

    GIVEN("-20 alone enabled and a SECC that negotiates TLS 1.2") {
        config.supported_protocols = {ProtocolId::ISO15118_20};
        iso15118::ev::Controller controller{config, callbacks};
        ControllerRun run{controller};

        THEN("the EV stops without sending a SupportedAppProtocolReq") {
            REQUIRE(pump_until(
                handler, [&]() { return stopped.load(); }, std::chrono::seconds(10)));
            REQUIRE(secc.received.empty());
        }
    }
}

SCENARIO("EV Connection under tls_version auto offers only the allowed TLS versions") {
    fd_event_handler handler;
    Connection client(handler);

    GIVEN("TLS 1.3 not allowed and a SECC that would negotiate TLS 1.3") {
        SeccListener secc(handler, any_version_secc_config(true));
        REQUIRE(secc.registered);
        auto params = auto_params();
        params.allow_tls_1_3 = false;

        THEN("TLS 1.2 is negotiated") {
            const auto result = round_trip(handler, client, secc, params);
            REQUIRE(result.connected == 1);
            REQUIRE(client.negotiated_tls_version() == TlsVersion::Tls1_2);
        }
    }

    GIVEN("TLS 1.2 not allowed and a TLS 1.2 SECC") {
        SeccListener secc(handler, secc_config(false, SECC_CHAIN, SECC_KEY, false));
        REQUIRE(secc.registered);
        auto params = auto_params();
        params.allow_tls_1_2 = false;

        THEN("the handshake fails") {
            const auto result = round_trip(handler, client, secc, params);
            REQUIRE(result.failed == 1);
            REQUIRE(result.connected == 0);
        }
    }
}

SCENARIO("EV Connection under tls_version auto logs a dropped TLS 1.3 once per process") {
    fd_event_handler handler;
    SeccListener secc(handler, any_version_secc_config(false));
    REQUIRE(secc.registered);
    LogCapture capture;
    auto params = auto_params();
    params.client_cert_chain_path = pki("certs/ca/vehicle/LOGGED_ONCE.pem");

    for (int attempt = 0; attempt < 2; ++attempt) {
        Connection client(handler);
        REQUIRE(round_trip(handler, client, secc, params).connected == 1);
    }

    REQUIRE(capture.count("LOGGED_ONCE.pem is missing or unreadable") == 1);
}

SCENARIO("EV Controller under tls_version auto offers only TLS versions its protocols run over") {
    using iso15118::ProtocolId;

    fd_event_handler handler;
    std::atomic_bool stopped{false};
    iso15118::ev::feedback::Callbacks callbacks{};
    callbacks.stopped = [&stopped]() { stopped = true; };

    GIVEN("only -2 enabled and a SECC that would negotiate TLS 1.3") {
        SeccListener secc(handler, any_version_secc_config(false));
        REQUIRE(secc.registered);
        auto config = auto_tls_controller_config(secc.port());
        config.supported_protocols = {ProtocolId::ISO15118_2};
        iso15118::ev::Controller controller{config, callbacks};
        ControllerRun run{controller};

        THEN("TLS 1.2 is negotiated and the SupportedAppProtocolReq offers -2") {
            REQUIRE(pump_until(
                handler, [&]() { return first_request(secc.received).has_value(); }, std::chrono::seconds(10)));
            const auto variant = first_request(secc.received);
            const auto* request = variant->get_if<iso15118::message_20::SupportedAppProtocolRequest>();
            REQUIRE(request != nullptr);
            REQUIRE(request->app_protocol.size() == 1);
            REQUIRE(request->app_protocol[0].protocol_namespace == iso15118::ISO2_NAMESPACE);
        }
    }

    GIVEN("only -20 enabled and a vehicle chain file that does not exist") {
        SeccListener secc(handler, any_version_secc_config(false));
        REQUIRE(secc.registered);
        auto config = auto_tls_controller_config(secc.port());
        config.supported_protocols = {ProtocolId::ISO15118_20};
        config.tls.client_cert_chain_path = pki("certs/ca/vehicle/MISSING.pem");
        LogCapture errors(iso15118::LogLevel::Error);
        iso15118::ev::Controller controller{config, callbacks};
        ControllerRun run{controller};

        THEN("the EV stops before connecting, naming the protocols and why TLS 1.3 is unavailable") {
            REQUIRE(pump_until(
                handler, [&]() { return stopped.load(); }, std::chrono::seconds(10)));
            REQUIRE(secc.peer == nullptr);
            REQUIRE(errors.contains("tls_version auto has no TLS version to offer for the enabled protocols "
                                    "(ISO15118-20): TLS 1.2 needs ISO 15118-2 in the offer; TLS 1.3 is unavailable: "
                                    "vehicle certificate chain"));
            REQUIRE(errors.contains("MISSING.pem is missing or unreadable"));
        }
    }

    GIVEN("only -20 enabled and a vehicle key that does not match the vehicle leaf") {
        SeccListener secc(handler, any_version_secc_config(false));
        REQUIRE(secc.registered);
        auto config = auto_tls_controller_config(secc.port());
        config.supported_protocols = {ProtocolId::ISO15118_20};
        config.tls.client_key_path = pki(SECC_KEY);
        LogCapture errors(iso15118::LogLevel::Error);
        iso15118::ev::Controller controller{config, callbacks};
        ControllerRun run{controller};

        THEN("the EV stops before connecting, naming the mismatch") {
            REQUIRE(pump_until(
                handler, [&]() { return stopped.load(); }, std::chrono::seconds(10)));
            REQUIRE(secc.peer == nullptr);
            REQUIRE(errors.contains("SECC_LEAF.key does not match the vehicle leaf certificate"));
        }
    }
}
