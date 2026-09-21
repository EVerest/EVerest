// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/session/secc_sap.hpp>

using namespace iso15118;
using ResponseCode = message_20::SupportedAppProtocolResponse::ResponseCode;

namespace {

message_20::SupportedAppProtocolRequest make_request(const std::string& protocol_namespace, uint32_t major,
                                                     uint32_t minor, uint8_t schema_id, uint8_t priority) {
    message_20::SupportedAppProtocolRequest req;
    req.app_protocol.push_back({protocol_namespace, major, minor, schema_id, priority});
    return req;
}

session::secc_sap::HandleResult negotiate(const message_20::SupportedAppProtocolRequest& req) {
    return session::secc_sap::handle_request(
        req, {ProtocolId::ISO15118_20, ProtocolId::ISO15118_2, ProtocolId::DIN70121}, {}, false, std::nullopt, false);
}

} // namespace

SCENARIO("SECC SupportedAppProtocol version matching [V2G2-170][V2G2-172]") {

    GIVEN("An ISO 15118-2 offer with matching major and minor version") {
        const auto result = negotiate(make_request(ISO2_NAMESPACE, 2, 0, 1, 1));
        THEN("The negotiation succeeds") {
            REQUIRE(result.response.response_code == ResponseCode::OK_SuccessfulNegotiation);
            REQUIRE(result.response.schema_id.has_value());
            REQUIRE(result.selected_namespace.value() == ISO2_NAMESPACE);
        }
    }

    GIVEN("An ISO 15118-2 offer with matching major but higher minor version") {
        const auto result = negotiate(make_request(ISO2_NAMESPACE, 2, 1, 1, 1));
        THEN("The negotiation succeeds with minor deviation") {
            REQUIRE(result.response.response_code == ResponseCode::OK_SuccessfulNegotiationWithMinorDeviation);
            REQUIRE(result.response.schema_id.has_value());
        }
    }

    GIVEN("An ISO 15118-2 offer with a non-matching major version") {
        const auto result = negotiate(make_request(ISO2_NAMESPACE, 3, 0, 1, 1));
        THEN("The negotiation fails (no supported protocol remains)") {
            REQUIRE(result.response.response_code == ResponseCode::Failed_NoNegotiation);
            REQUIRE(not result.response.schema_id.has_value());
            REQUIRE(not result.selected_namespace.has_value());
        }
    }

    GIVEN("A DIN SPEC 70121 offer with a non-matching major version") {
        const auto result = negotiate(make_request(DIN70121_NAMESPACE, 1, 0, 3, 1));
        THEN("The negotiation fails") {
            REQUIRE(result.response.response_code == ResponseCode::Failed_NoNegotiation);
        }
    }

    GIVEN("A non-matching ISO 15118-2 major version alongside a supported ISO 15118-20 offer") {
        message_20::SupportedAppProtocolRequest req;
        req.app_protocol.push_back({ISO2_NAMESPACE, 3, 0, 1, 1}); // highest priority, unsupported major
        req.app_protocol.push_back({ISO20_DC_PROTOCOL_NAMESPACE, 1, 0, 2, 2});

        const auto result = negotiate(req);
        THEN("The supported ISO 15118-20 protocol is selected instead") {
            REQUIRE(result.response.response_code == ResponseCode::OK_SuccessfulNegotiation);
            REQUIRE(result.response.schema_id.value() == 2);
            REQUIRE(result.selected_namespace.value() == ISO20_DC_PROTOCOL_NAMESPACE);
        }
    }
}
