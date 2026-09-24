// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/session/secc_sap.hpp>

using namespace iso15118;
using ResponseCode = message_20::SupportedAppProtocolResponse::ResponseCode;
namespace dt = message_20::datatypes;

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

SCENARIO("SECC SupportedAppProtocol namespace follows the offered energy transfer") {
    const std::vector<ProtocolId> protocols{ProtocolId::ISO15118_20, ProtocolId::ISO15118_2};
    const std::vector<dt::ServiceCategory> dc_services{dt::ServiceCategory::DC, dt::ServiceCategory::DC_BPT};
    const std::vector<dt::ServiceCategory> ac_services{dt::ServiceCategory::AC, dt::ServiceCategory::AC_BPT};

    message_20::SupportedAppProtocolRequest ac_first;
    ac_first.app_protocol.push_back({ISO20_AC_PROTOCOL_NAMESPACE, 1, 0, 1, 1});
    ac_first.app_protocol.push_back({ISO20_DC_PROTOCOL_NAMESPACE, 1, 0, 2, 2});
    ac_first.app_protocol.push_back({ISO2_NAMESPACE, 2, 0, 3, 3});

    GIVEN("A DC charger and an EV ranking -20:AC above -20:DC") {
        const auto result =
            session::secc_sap::handle_request(ac_first, protocols, dc_services, false, std::nullopt, true);
        THEN("-20:DC is selected") {
            REQUIRE(result.response.response_code == ResponseCode::OK_SuccessfulNegotiation);
            REQUIRE(result.response.schema_id.value() == 2);
            REQUIRE(result.selected_namespace.value() == ISO20_DC_PROTOCOL_NAMESPACE);
        }
    }

    GIVEN("An AC charger and an EV ranking -20:DC above -20:AC") {
        message_20::SupportedAppProtocolRequest dc_first;
        dc_first.app_protocol.push_back({ISO20_DC_PROTOCOL_NAMESPACE, 1, 0, 1, 1});
        dc_first.app_protocol.push_back({ISO20_AC_PROTOCOL_NAMESPACE, 1, 0, 2, 2});

        const auto result =
            session::secc_sap::handle_request(dc_first, protocols, ac_services, false, std::nullopt, true);
        THEN("-20:AC is selected") {
            REQUIRE(result.selected_namespace.value() == ISO20_AC_PROTOCOL_NAMESPACE);
        }
    }

    GIVEN("A DC charger and an EV offering -20:AC above ISO 15118-2") {
        message_20::SupportedAppProtocolRequest req;
        req.app_protocol.push_back({ISO20_AC_PROTOCOL_NAMESPACE, 1, 0, 1, 1});
        req.app_protocol.push_back({ISO2_NAMESPACE, 2, 0, 2, 2});

        const auto result = session::secc_sap::handle_request(req, protocols, dc_services, false, std::nullopt, true);
        THEN("ISO 15118-2 is selected, as it can carry DC") {
            REQUIRE(result.selected_namespace.value() == ISO2_NAMESPACE);
        }
    }

    GIVEN("A DC charger and an EV offering only -20:AC") {
        const auto req = make_request(ISO20_AC_PROTOCOL_NAMESPACE, 1, 0, 1, 1);

        WHEN("selecting_sap_based_on_energy_service is disabled") {
            const auto result =
                session::secc_sap::handle_request(req, protocols, dc_services, false, std::nullopt, true);
            THEN("-20:AC is still accepted (ISO 15118-20 AMD1)") {
                REQUIRE(result.response.response_code == ResponseCode::OK_SuccessfulNegotiation);
                REQUIRE(result.selected_namespace.value() == ISO20_AC_PROTOCOL_NAMESPACE);
            }
        }

        WHEN("selecting_sap_based_on_energy_service is enabled") {
            const auto result =
                session::secc_sap::handle_request(req, protocols, dc_services, true, std::nullopt, true);
            THEN("The negotiation fails") {
                REQUIRE(result.response.response_code == ResponseCode::Failed_NoNegotiation);
            }
        }
    }

    GIVEN("No energy services known yet") {
        const auto result = session::secc_sap::handle_request(ac_first, protocols, {}, true, std::nullopt, true);
        THEN("The EV's highest priority is selected") {
            REQUIRE(result.selected_namespace.value() == ISO20_AC_PROTOCOL_NAMESPACE);
        }
    }
}
