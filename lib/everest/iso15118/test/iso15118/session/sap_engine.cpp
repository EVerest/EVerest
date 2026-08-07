// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Engine-level tests of the SupportedAppProtocol handshake: the negotiation itself is covered in
// secc_sap.cpp, this drives the SapEngine the way the Session does (feed a serialized request, drain
// the staged response, take the handover result) and pins the states in between.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <iso15118/io/logging.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/variant.hpp>
#include <iso15118/session/config.hpp>
#include <iso15118/session/sap_engine.hpp>

using namespace iso15118;

namespace {

message_20::SupportedAppProtocol make_protocol(const std::string& protocol_namespace, uint32_t major, uint32_t minor,
                                               uint8_t schema_id, uint8_t priority) {
    return {protocol_namespace, major, minor, schema_id, priority};
}

// Drives a SapEngine over an output buffer of its own, standing in for the Session's response buffer.
class SapEngineHelper {
public:
    SapEngineHelper(std::vector<ProtocolId> supported_protocols, bool tls_active) :
        config(make_config(std::move(supported_protocols))),
        engine(io::StreamOutputView{buffer.data(), buffer.size()}, config, make_callbacks(), tls_active) {
        io::set_logging_callback([](LogLevel, std::string) {});
    }

    SapEngine& get_engine() {
        return engine;
    }

    // Serialize \p req into a scratch buffer and hand it to the engine, as the Session does with the
    // payload of a received V2GTP packet.
    void feed(const message_20::SupportedAppProtocolRequest& req) {
        feed_serialized(req);
    }

    // Anything but a request: the handshake only ever expects a SupportedAppProtocolReq.
    void feed(const message_20::SupportedAppProtocolResponse& res) {
        feed_serialized(res);
    }

    // Decode whatever the engine staged in the output buffer.
    message_20::SupportedAppProtocolResponse take_response() {
        const auto outgoing = engine.take_outgoing();
        REQUIRE(outgoing.has_value());
        REQUIRE(outgoing->payload_type == io::v2gtp::PayloadType::SAP);
        REQUIRE(outgoing->message_type == V2gMessageType{message_20::Type::SupportedAppProtocolRes});

        const message_20::Variant variant{io::v2gtp::PayloadType::SAP,
                                          io::StreamInputView{buffer.data(), outgoing->payload_size}};
        const auto* res = variant.get_if<message_20::SupportedAppProtocolResponse>();
        REQUIRE(res != nullptr);
        return *res;
    }

    std::vector<std::string> selected_protocols;

private:
    static session::SessionConfig make_config(std::vector<ProtocolId> supported_protocols) {
        session::EvseSetupConfig setup;
        setup.supported_protocols = std::move(supported_protocols);
        return session::SessionConfig{std::move(setup)};
    }

    session::feedback::Callbacks make_callbacks() {
        session::feedback::Callbacks callbacks;
        callbacks.selected_protocol = [this](const std::string& protocol) { selected_protocols.push_back(protocol); };
        return callbacks;
    }

    template <typename Message> void feed_serialized(const Message& message) {
        std::array<uint8_t, 1024> request_buffer{};
        const auto len =
            message_20::serialize(message, io::StreamOutputView{request_buffer.data(), request_buffer.size()});
        engine.on_packet(io::v2gtp::PayloadType::SAP, io::StreamInputView{request_buffer.data(), len});
    }

    std::array<uint8_t, 4096> buffer{};
    session::SessionConfig config;
    SapEngine engine;
};

} // namespace

SCENARIO("SapEngine hands the session over to the negotiated protocol") {

    GIVEN("An SECC supporting ISO 15118-2 and an EV offering it") {
        SapEngineHelper helper{{ProtocolId::ISO15118_2}, false};
        auto& engine = helper.get_engine();

        THEN("Nothing is staged before the request arrives") {
            REQUIRE_FALSE(engine.has_outgoing());
            REQUIRE_FALSE(engine.is_finished());
            REQUIRE_FALSE(engine.take_negotiated().has_value());
            // The handshake response is paced like the pre-20 protocols.
            REQUIRE(engine.delay_response_after_request());
        }

        message_20::SupportedAppProtocolRequest req;
        req.app_protocol.push_back(make_protocol(ISO2_NAMESPACE, 2, 0, 7, 1));
        helper.feed(req);

        THEN("The response is staged and the session is not over") {
            REQUIRE(engine.has_outgoing());
            // Must stay false while the response is staged, otherwise the Session tears down before it
            // reaches the EV.
            REQUIRE_FALSE(engine.is_finished());
            REQUIRE_FALSE(engine.is_paused());
            REQUIRE_FALSE(engine.is_finished_with_error());
        }

        THEN("The negotiated protocol is reported to the module") {
            REQUIRE(helper.selected_protocols == std::vector<std::string>{"ISO15118-2-2013"});
        }

        THEN("The staged response confirms the offered schema") {
            const auto res = helper.take_response();
            REQUIRE(res.response_code ==
                    message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation);
            REQUIRE(res.schema_id.has_value());
            REQUIRE(res.schema_id.value() == 7);
            REQUIRE_FALSE(engine.has_outgoing());
            REQUIRE_FALSE(engine.take_outgoing().has_value());
        }

        THEN("The handover carries the protocol and the confirmed app protocol entry") {
            const auto negotiated = engine.take_negotiated();
            REQUIRE(negotiated.has_value());
            REQUIRE(negotiated->protocol_id == ProtocolId::ISO15118_2);
            REQUIRE(negotiated->selected_protocol.schema_id == 7);
            REQUIRE(negotiated->selected_protocol.protocol_namespace == ISO2_NAMESPACE);
            REQUIRE(negotiated->offered_protocols.size() == 1);

            AND_THEN("It is handed out exactly once") {
                REQUIRE_FALSE(engine.take_negotiated().has_value());
            }
        }

        THEN("The session continues once the response was drained") {
            helper.take_response();
            REQUIRE_FALSE(engine.is_finished());
        }
    }

    GIVEN("An SECC supporting ISO 15118-20 and an EV offering the DC namespace") {
        SapEngineHelper helper{{ProtocolId::ISO15118_20}, false};
        auto& engine = helper.get_engine();

        message_20::SupportedAppProtocolRequest req;
        req.app_protocol.push_back(make_protocol(ISO20_DC_PROTOCOL_NAMESPACE, 1, 0, 2, 1));
        helper.feed(req);

        THEN("The -20 engine takes over") {
            const auto negotiated = engine.take_negotiated();
            REQUIRE(negotiated.has_value());
            REQUIRE(negotiated->protocol_id == ProtocolId::ISO15118_20);
            REQUIRE(helper.selected_protocols == std::vector<std::string>{"ISO15118-20:DC"});
        }
    }
}

SCENARIO("SapEngine ends the session when no protocol can be negotiated") {

    GIVEN("An SECC supporting only ISO 15118-20 and an EV offering only DIN SPEC 70121") {
        SapEngineHelper helper{{ProtocolId::ISO15118_20}, false};
        auto& engine = helper.get_engine();

        message_20::SupportedAppProtocolRequest req;
        req.app_protocol.push_back(make_protocol(DIN70121_NAMESPACE, 2, 0, 1, 1));
        helper.feed(req);

        THEN("The negotiation fails but the session only ends after the response was flushed") {
            REQUIRE(engine.has_outgoing());
            REQUIRE_FALSE(engine.is_finished());

            const auto res = helper.take_response();
            REQUIRE(res.response_code == message_20::SupportedAppProtocolResponse::ResponseCode::Failed_NoNegotiation);

            REQUIRE(engine.is_finished());
            REQUIRE_FALSE(engine.take_negotiated().has_value());
            // A failed handshake terminates through the driver-stopped path, not the error-close one.
            REQUIRE_FALSE(engine.is_finished_with_error());
        }
    }

    GIVEN("A TLS connection and an EV offering only DIN SPEC 70121 [V2G-DC-869]") {
        SapEngineHelper helper{{ProtocolId::DIN70121}, true};
        auto& engine = helper.get_engine();

        message_20::SupportedAppProtocolRequest req;
        req.app_protocol.push_back(make_protocol(DIN70121_NAMESPACE, 2, 0, 1, 1));
        helper.feed(req);

        THEN("DIN is not negotiated over TLS and the handshake fails") {
            const auto res = helper.take_response();
            REQUIRE(res.response_code == message_20::SupportedAppProtocolResponse::ResponseCode::Failed_NoNegotiation);
            REQUIRE(engine.is_finished());
            REQUIRE(helper.selected_protocols.empty());
        }
    }

    GIVEN("An EV sending something other than a SupportedAppProtocolReq") {
        SapEngineHelper helper{{ProtocolId::ISO15118_20}, false};
        auto& engine = helper.get_engine();

        message_20::SupportedAppProtocolResponse res;
        res.response_code = message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation;
        res.schema_id = 1;
        helper.feed(res);

        THEN("No response is staged and the session ends immediately") {
            REQUIRE_FALSE(engine.has_outgoing());
            REQUIRE(engine.is_finished());
            REQUIRE_FALSE(engine.take_negotiated().has_value());
        }
    }

    GIVEN("An EV that connects but never sends the request") {
        SapEngineHelper helper{{ProtocolId::ISO15118_20}, false};
        auto& engine = helper.get_engine();

        engine.on_timeout(d20::TimeoutType::SEQUENCE);

        THEN("The sequence timeout ends the session") {
            REQUIRE(engine.is_finished());
            REQUIRE_FALSE(engine.has_outgoing());
        }
    }
}
