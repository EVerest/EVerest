// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <vector>

#include <iso15118/detail/d20/ev/state/supported_app_protocol.hpp>
#include <iso15118/session/protocol.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;
namespace sap = d20::ev::state::supported_app_protocol;

namespace {

// Resolves the ProtocolId a negotiated schema_id maps back to, mirroring the EvSession bookkeeping.
std::optional<ProtocolId> protocol_for_schema(const message_20::SupportedAppProtocolRequest& req, uint8_t schema_id) {
    for (const auto& proto : req.app_protocol) {
        if (proto.schema_id == schema_id) {
            return protocol_id_from_namespace(proto.protocol_namespace);
        }
    }
    return std::nullopt;
}

} // namespace

SCENARIO("EVCC SupportedAppProtocol multi-protocol offer builder") {

    GIVEN("An EV offering ISO 15118-2 and DIN for a DC service") {
        const auto req = sap::create_request({ProtocolId::ISO15118_2, ProtocolId::DIN70121}, {dt::ServiceCategory::DC},
                                             std::nullopt);

        THEN("Both are offered in priority order with the expected versions and unique schema ids") {
            REQUIRE(req.app_protocol.size() == 2);

            REQUIRE(req.app_protocol[0].protocol_namespace == ISO2_NAMESPACE);
            REQUIRE(req.app_protocol[0].version_number_major == 2);
            REQUIRE(req.app_protocol[0].version_number_minor == 0);
            REQUIRE(req.app_protocol[0].priority == 1);
            REQUIRE(req.app_protocol[0].schema_id == 1);

            REQUIRE(req.app_protocol[1].protocol_namespace == DIN70121_NAMESPACE);
            REQUIRE(req.app_protocol[1].version_number_major == 2);
            REQUIRE(req.app_protocol[1].version_number_minor == 0);
            REQUIRE(req.app_protocol[1].priority == 2);
            REQUIRE(req.app_protocol[1].schema_id == 2);
        }
    }

    GIVEN("An EV offering only ISO 15118-2 for an AC service") {
        const auto req = sap::create_request({ProtocolId::ISO15118_2}, {dt::ServiceCategory::AC}, std::nullopt);

        THEN("Only the ISO-2 namespace is offered") {
            REQUIRE(req.app_protocol.size() == 1);
            REQUIRE(req.app_protocol[0].protocol_namespace == ISO2_NAMESPACE);
            REQUIRE(req.app_protocol[0].priority == 1);
        }
    }

    GIVEN("An EV offering ISO 15118-20 (DC), ISO 15118-2 and DIN for a DC service") {
        const auto req = sap::create_request({ProtocolId::ISO15118_20, ProtocolId::ISO15118_2, ProtocolId::DIN70121},
                                             {dt::ServiceCategory::DC}, std::nullopt);

        THEN("All three are offered in priority order with the -20 DC namespace first") {
            REQUIRE(req.app_protocol.size() == 3);

            REQUIRE(req.app_protocol[0].protocol_namespace == ISO20_DC_PROTOCOL_NAMESPACE);
            REQUIRE(req.app_protocol[0].version_number_major == 1);
            REQUIRE(req.app_protocol[0].priority == 1);

            REQUIRE(req.app_protocol[1].protocol_namespace == ISO2_NAMESPACE);
            REQUIRE(req.app_protocol[1].priority == 2);
            REQUIRE(req.app_protocol[1].schema_id == 2);

            REQUIRE(req.app_protocol[2].protocol_namespace == DIN70121_NAMESPACE);
            REQUIRE(req.app_protocol[2].priority == 3);
            REQUIRE(req.app_protocol[2].schema_id == 3);
        }
    }

    GIVEN("An EV offering ISO 15118-2 and DIN for an AC service") {
        const auto req = sap::create_request({ProtocolId::ISO15118_2, ProtocolId::DIN70121}, {dt::ServiceCategory::AC},
                                             std::nullopt);

        THEN("DIN (DC-only) is excluded and only ISO-2 is offered") {
            REQUIRE(req.app_protocol.size() == 1);
            REQUIRE(req.app_protocol[0].protocol_namespace == ISO2_NAMESPACE);
        }
    }

    GIVEN("A multi-protocol DC offer and a negotiation response") {
        const auto req = sap::create_request({ProtocolId::ISO15118_20, ProtocolId::ISO15118_2, ProtocolId::DIN70121},
                                             {dt::ServiceCategory::DC}, std::nullopt);

        THEN("Each returned schema id resolves back to the correct ProtocolId") {
            REQUIRE(protocol_for_schema(req, 1) == ProtocolId::ISO15118_20);
            REQUIRE(protocol_for_schema(req, 2) == ProtocolId::ISO15118_2);
            REQUIRE(protocol_for_schema(req, 3) == ProtocolId::DIN70121);

            message_20::SupportedAppProtocolResponse res;
            res.response_code = message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation;
            res.schema_id = 3;
            const auto result = sap::handle_response(res);
            REQUIRE(result.negotiated);
            REQUIRE(result.schema_id.has_value());
            REQUIRE(protocol_for_schema(req, result.schema_id.value()) == ProtocolId::DIN70121);
        }
    }
}
