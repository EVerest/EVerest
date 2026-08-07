// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
//
// The SupportedAppProtocol negotiation is now run by the SECC session driver (not the FSM), so it is
// covered here at function level against session::secc_sap::handle_request, the SECC counterpart of the
// EVCC ev_sap free functions.
#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <vector>

#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/session/protocol.hpp>
#include <iso15118/session/secc_sap.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;

using ResponseCode = message_20::SupportedAppProtocolResponse::ResponseCode;

namespace {

message_20::SupportedAppProtocol make_app_protocol(const std::string& protocol_namespace, uint8_t schema_id,
                                                   uint8_t priority, uint32_t version_major = 1,
                                                   uint32_t version_minor = 0) {
    return {protocol_namespace, version_major, version_minor, schema_id, priority};
}

} // namespace

SCENARIO("ISO15118-20 SECC supported app protocol negotiation") {

    const std::vector<ProtocolId> supported_protocols = {ProtocolId::ISO15118_20};
    const std::vector<dt::ServiceCategory> supported_energy_services = {dt::ServiceCategory::DC};
    const std::optional<std::string> custom_namespace = "urn:iso:std:iso:15118:-20:AABB";

    GIVEN("Good case - DC") {
        message_20::SupportedAppProtocolRequest req;
        req.app_protocol.push_back(make_app_protocol("urn:iso:std:iso:15118:-20:DC", 1, 1));

        const auto result = session::secc_sap::handle_request(req, supported_protocols, supported_energy_services,
                                                              false, custom_namespace, /*tls_active=*/false);

        THEN("Negotiation succeeds with the DC schema") {
            REQUIRE(result.response.response_code == ResponseCode::OK_SuccessfulNegotiation);
            REQUIRE(result.response.schema_id.value_or(0) == 1);
            REQUIRE(result.selected_namespace.has_value());
            REQUIRE(result.selected_namespace.value() == "urn:iso:std:iso:15118:-20:DC");
        }
    }

    GIVEN("Good case - Custom") {
        message_20::SupportedAppProtocolRequest req;
        req.app_protocol.push_back(make_app_protocol("urn:iso:std:iso:15118:-20:AABB", 1, 1));

        const auto result = session::secc_sap::handle_request(req, supported_protocols, supported_energy_services,
                                                              false, custom_namespace, /*tls_active=*/false);

        THEN("Negotiation succeeds on the custom namespace") {
            REQUIRE(result.response.response_code == ResponseCode::OK_SuccessfulNegotiation);
            REQUIRE(result.response.schema_id.value_or(0) == 1);
            REQUIRE(result.selected_namespace.value() == "urn:iso:std:iso:15118:-20:AABB");
        }
    }

    GIVEN("Good case - Priority") {
        message_20::SupportedAppProtocolRequest req;
        req.app_protocol.push_back(make_app_protocol("urn:iso:std:iso:15118:-20:DC", 1, 2));
        req.app_protocol.push_back(make_app_protocol("urn:iso:std:iso:15118:-20:AC", 3, 1));

        const auto result = session::secc_sap::handle_request(req, supported_protocols, supported_energy_services,
                                                              false, custom_namespace, /*tls_active=*/false);

        THEN("The highest-priority match wins") {
            REQUIRE(result.response.response_code == ResponseCode::OK_SuccessfulNegotiation);
            REQUIRE(result.response.schema_id.value_or(0) == 3);
            REQUIRE(result.selected_namespace.value() == "urn:iso:std:iso:15118:-20:AC");
        }
    }

    GIVEN("Bad case - Unknown protocol namespace") {
        message_20::SupportedAppProtocolRequest req;
        req.app_protocol.push_back(make_app_protocol("Foobar", 12, 1));

        const auto result = session::secc_sap::handle_request(req, supported_protocols, supported_energy_services,
                                                              false, custom_namespace, /*tls_active=*/false);

        THEN("Negotiation fails") {
            REQUIRE(result.response.response_code == ResponseCode::Failed_NoNegotiation);
            REQUIRE(not result.selected_namespace.has_value());
        }
    }

    GIVEN("Bad case - empty app protocol") {
        message_20::SupportedAppProtocolRequest req;
        req.app_protocol.emplace_back();

        const auto result = session::secc_sap::handle_request(req, supported_protocols, supported_energy_services,
                                                              false, custom_namespace, /*tls_active=*/false);

        THEN("Negotiation fails") {
            REQUIRE(result.response.response_code == ResponseCode::Failed_NoNegotiation);
        }
    }

    GIVEN("Good case - Selecting namespace based on supported energy services") {
        message_20::SupportedAppProtocolRequest req;
        req.app_protocol.push_back(make_app_protocol("urn:iso:std:iso:15118:-20:DC", 2, 2));
        req.app_protocol.push_back(make_app_protocol("urn:iso:std:iso:15118:-20:AC", 1, 1));

        // With energy-service-based selection and a DC-only SECC, the higher-priority AC offer is
        // rejected and the DC schema is chosen instead.
        const auto result = session::secc_sap::handle_request(req, supported_protocols, supported_energy_services, true,
                                                              custom_namespace, /*tls_active=*/false);

        THEN("The DC schema is negotiated despite lower priority") {
            REQUIRE(result.response.response_code == ResponseCode::OK_SuccessfulNegotiation);
            REQUIRE(result.response.schema_id.value_or(0) == 2);
            REQUIRE(result.selected_namespace.value() == "urn:iso:std:iso:15118:-20:DC");
        }
    }

    GIVEN("Protocol not in the configured supported set") {
        message_20::SupportedAppProtocolRequest req;
        req.app_protocol.push_back(make_app_protocol("urn:iso:std:iso:15118:-20:DC", 1, 1));

        // ISO 15118-20 is offered by the EV but the SECC is configured to only support ISO 15118-2.
        const auto result = session::secc_sap::handle_request(req, {ProtocolId::ISO15118_2}, supported_energy_services,
                                                              false, std::nullopt, /*tls_active=*/false);

        THEN("Negotiation fails") {
            REQUIRE(result.response.response_code == ResponseCode::Failed_NoNegotiation);
        }
    }

    // [V2G-DC-869]: DIN SPEC 70121 is plaintext-only and must not be negotiated over TLS.
    GIVEN("A DIN-only offer over a TLS connection") {
        message_20::SupportedAppProtocolRequest req;
        req.app_protocol.push_back(make_app_protocol("urn:din:70121:2012:MsgDef", 2, 1, 2, 0));

        WHEN("the connection is plaintext") {
            const auto result = session::secc_sap::handle_request(req, {ProtocolId::DIN70121},
                                                                  supported_energy_services, false, std::nullopt,
                                                                  /*tls_active=*/false);
            THEN("DIN is negotiated") {
                REQUIRE(result.response.response_code == ResponseCode::OK_SuccessfulNegotiation);
                REQUIRE(result.selected_namespace.value() == "urn:din:70121:2012:MsgDef");
            }
        }

        WHEN("the connection is TLS") {
            const auto result = session::secc_sap::handle_request(req, {ProtocolId::DIN70121},
                                                                  supported_energy_services, false, std::nullopt,
                                                                  /*tls_active=*/true);
            THEN("negotiation fails - DIN is not offered over TLS") {
                REQUIRE(result.response.response_code == ResponseCode::Failed_NoNegotiation);
            }
        }
    }
}
