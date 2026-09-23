// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <arpa/inet.h>

#include <catch2/catch_test_macros.hpp>

#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/session/feedback.hpp>
#include <iso15118/session/iso.hpp>
#include <iso15118/session/protocol.hpp>

#include "mock_connection.hpp"

using iso15118::ProtocolId;
using iso15118::test::MockConnection;

namespace {

std::vector<uint8_t> serialize_sap_request(const std::string& protocol_namespace, uint32_t major, uint8_t schema_id) {
    iso15118::message_20::SupportedAppProtocolRequest req;
    req.app_protocol.push_back({protocol_namespace, major, 0, schema_id, 1});

    std::array<uint8_t, 1024> payload{};
    const auto payload_len =
        iso15118::message_20::serialize(req, iso15118::io::StreamOutputView{payload.data(), payload.size()});

    std::vector<uint8_t> frame(iso15118::io::SdpPacket::V2GTP_HEADER_SIZE + payload_len);
    frame[0] = iso15118::io::SDP_PROTOCOL_VERSION;
    frame[1] = iso15118::io::SDP_INVERSE_PROTOCOL_VERSION;

    const uint16_t type = htons(static_cast<uint16_t>(iso15118::io::v2gtp::PayloadType::SAP));
    std::memcpy(frame.data() + 2, &type, sizeof(type));

    const uint32_t len = htonl(static_cast<uint32_t>(payload_len));
    std::memcpy(frame.data() + 4, &len, sizeof(len));

    std::memcpy(frame.data() + iso15118::io::SdpPacket::V2GTP_HEADER_SIZE, payload.data(), payload_len);
    return frame;
}

struct Fixture {
    iso15118::session::feedback::Callbacks callbacks;
    iso15118::session::SessionConfig session_config{iso15118::session::EvseSetupConfig{}};
    std::optional<iso15118::d20::PauseContext> pause_ctx{std::nullopt};
    std::optional<iso15118::d2::PauseContext> d2_pause_ctx{std::nullopt};
    MockConnection* conn{nullptr};
    std::unique_ptr<iso15118::Session> session;

    explicit Fixture(std::vector<ProtocolId> supported, bool accept = true) {
        callbacks.signal = [](auto) {};
        session_config.supported_protocols = std::move(supported);
        auto connection = std::make_unique<MockConnection>();
        conn = connection.get();
        session = std::make_unique<iso15118::Session>(std::move(connection), session_config, callbacks, pause_ctx,
                                                      d2_pause_ctx);
        if (accept) {
            conn->fire(iso15118::io::ConnectionEvent::ACCEPTED);
        }
    }

    void run_handshake(const std::vector<uint8_t>& frame) {
        conn->queue_raw(frame.data(), frame.size());
        conn->fire(iso15118::io::ConnectionEvent::NEW_DATA);
        // The SupportedAppProtocolRes is paced, so the second poll hands the session to the engine.
        session->poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        session->poll();
    }
};

} // namespace

SCENARIO("The communication setup timer anchor follows the negotiated protocol") {
    GIVEN("a session before the connection is accepted") {
        Fixture fixture{{ProtocolId::ISO15118_20}, false};

        THEN("there is no TCP/TLS anchor to report") {
            REQUIRE_FALSE(fixture.session->get_tcp_setup_timer_anchor().has_value());
        }
    }

    GIVEN("a connection that has been accepted but not negotiated") {
        Fixture fixture{{ProtocolId::ISO15118_20}};

        THEN("the later ISO 15118-20 anchor applies until the protocol is known") {
            REQUIRE(fixture.session->get_tcp_setup_timer_anchor().has_value());
        }
    }

    GIVEN("an ISO 15118-20 handshake") {
        const auto before_accept = iso15118::get_current_time_point();
        Fixture fixture{{ProtocolId::ISO15118_20}};
        fixture.run_handshake(serialize_sap_request(iso15118::ISO20_AC_PROTOCOL_NAMESPACE, 1, 1));

        THEN("the anchor stays at the TCP/TLS establishment time [V2G20-2303]") {
            const auto anchor = fixture.session->get_tcp_setup_timer_anchor();
            REQUIRE(anchor.has_value());
            REQUIRE(anchor.value() >= before_accept);
            REQUIRE(anchor.value() <= iso15118::get_current_time_point());
        }
    }

    GIVEN("an ISO 15118-2 handshake") {
        Fixture fixture{{ProtocolId::ISO15118_2}};
        fixture.run_handshake(serialize_sap_request(iso15118::ISO2_NAMESPACE, 2, 7));

        THEN("the timer falls back to the D-LINK anchor [V2G2-714]") {
            REQUIRE_FALSE(fixture.conn->written.empty());
            REQUIRE_FALSE(fixture.session->get_tcp_setup_timer_anchor().has_value());
        }
    }

    GIVEN("a DIN SPEC 70121 handshake") {
        Fixture fixture{{ProtocolId::DIN70121}};
        fixture.run_handshake(serialize_sap_request(iso15118::DIN70121_NAMESPACE, 2, 1));

        THEN("the timer falls back to the D-LINK anchor") {
            REQUIRE_FALSE(fixture.conn->written.empty());
            REQUIRE_FALSE(fixture.session->get_tcp_setup_timer_anchor().has_value());
        }
    }
}

SCENARIO("A session awaits the EV's connection until TCP/TLS is accepted") {
    GIVEN("a session created from an SDP request whose connection is not accepted yet") {
        Fixture fixture{{ProtocolId::ISO15118_20}, false};

        THEN("it is awaiting the connection, so a repeated SDP request is answered again") {
            REQUIRE(fixture.session->awaiting_connection());
        }
    }

    GIVEN("a session whose connection has been accepted") {
        Fixture fixture{{ProtocolId::ISO15118_20}};

        THEN("it no longer awaits the connection") {
            REQUIRE_FALSE(fixture.session->awaiting_connection());
        }
    }

    GIVEN("a session whose accepted connection has closed again") {
        Fixture fixture{{ProtocolId::ISO15118_20}};
        fixture.conn->fire(iso15118::io::ConnectionEvent::CLOSED);

        THEN("it does not go back to awaiting a connection") {
            REQUIRE_FALSE(fixture.session->awaiting_connection());
        }
    }
}
