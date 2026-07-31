// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d20/ev/state/supported_app_protocol.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;

SCENARIO("EVCC SupportedAppProtocol request/response handling") {

    GIVEN("A DC-only EV") {
        const auto req = d20::ev::state::supported_app_protocol::create_request(
            {ProtocolId::ISO15118_20}, {dt::ServiceCategory::DC}, std::nullopt);

        THEN("A single DC app protocol is offered with schema id and priority 1") {
            REQUIRE(req.app_protocol.size() == 1);
            REQUIRE(req.app_protocol[0].protocol_namespace == ISO20_DC_PROTOCOL_NAMESPACE);
            REQUIRE(req.app_protocol[0].version_number_major == 1);
            REQUIRE(req.app_protocol[0].version_number_minor == 0);
            REQUIRE(req.app_protocol[0].schema_id == 1);
            REQUIRE(req.app_protocol[0].priority == 1);
        }
    }

    GIVEN("An AC EV") {
        const auto req = d20::ev::state::supported_app_protocol::create_request(
            {ProtocolId::ISO15118_20}, {dt::ServiceCategory::AC_BPT}, std::nullopt);
        THEN("The AC namespace is offered") {
            REQUIRE(req.app_protocol.size() == 1);
            REQUIRE(req.app_protocol[0].protocol_namespace == ISO20_AC_PROTOCOL_NAMESPACE);
        }
    }

    GIVEN("An EV supporting both DC and AC in priority order") {
        const auto req = d20::ev::state::supported_app_protocol::create_request(
            {ProtocolId::ISO15118_20}, {dt::ServiceCategory::DC, dt::ServiceCategory::AC}, std::nullopt);
        THEN("Both namespaces are offered in priority order with increasing counters") {
            REQUIRE(req.app_protocol.size() == 2);
            REQUIRE(req.app_protocol[0].protocol_namespace == ISO20_DC_PROTOCOL_NAMESPACE);
            REQUIRE(req.app_protocol[0].priority == 1);
            REQUIRE(req.app_protocol[1].protocol_namespace == ISO20_AC_PROTOCOL_NAMESPACE);
            REQUIRE(req.app_protocol[1].priority == 2);
            REQUIRE(req.app_protocol[1].schema_id == 2);
        }
    }

    GIVEN("A successful negotiation response") {
        message_20::SupportedAppProtocolResponse res;
        res.response_code = message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation;
        res.schema_id = 1;

        const auto result = d20::ev::state::supported_app_protocol::handle_response(res);
        THEN("The result reports the negotiated schema") {
            REQUIRE(result.negotiated == true);
            REQUIRE(result.schema_id.has_value());
            REQUIRE(result.schema_id.value() == 1);
        }
    }

    GIVEN("A failed negotiation response") {
        message_20::SupportedAppProtocolResponse res;
        res.response_code = message_20::SupportedAppProtocolResponse::ResponseCode::Failed_NoNegotiation;

        const auto result = d20::ev::state::supported_app_protocol::handle_response(res);
        THEN("The result reports no negotiation") {
            REQUIRE(result.negotiated == false);
        }
    }
}
