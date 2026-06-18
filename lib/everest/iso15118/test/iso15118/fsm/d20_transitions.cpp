// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <optional>
#include <string>
#include <vector>

#include <iso15118/d20/state/dc_charge_loop.hpp>

#include <iso15118/message/dc_charge_loop.hpp>
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

        // Both energy families offered, so only the EV's priority decides.
        const std::vector<dt::ServiceCategory> ac_and_dc = {dt::ServiceCategory::AC, dt::ServiceCategory::DC};
        const auto result = session::secc_sap::handle_request(req, supported_protocols, ac_and_dc, false,
                                                              custom_namespace, /*tls_active=*/false);

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

        // With a DC-only SECC the higher-priority AC offer is rejected and the DC schema chosen instead.
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

SCENARIO("ISO15118-20 DC charge loop - error forced via ErrorShutdown") {

    namespace dt = message_20::datatypes;

    const auto evse_id = std::string("everest se");
    const std::vector<dt::ServiceCategory> supported_energy_services = {dt::ServiceCategory::DC};
    const auto cert_install{false};
    const std::vector<uint16_t> vas_services{};
    const std::vector<dt::Authorization> auth_services = {dt::Authorization::EIM};

    d20::DcTransferLimits dc_limits;
    d20::AcTransferLimits ac_limits;
    d20::DcTransferLimits powersupply_limits;
    dc_limits.charge_limits.power.max = {22, 3};
    dc_limits.charge_limits.power.min = {10, 0};
    dc_limits.charge_limits.current.max = {250, 0};
    dc_limits.voltage.max = {900, 0};

    const std::vector<d20::ControlMobilityNeedsModes> control_mobility_modes = {
        {dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc}};

    session::EvseSetupConfig evse_setup{};
    evse_setup.evse_id = evse_id;
    evse_setup.supported_energy_services = supported_energy_services;
    evse_setup.authorization_services = auth_services;
    evse_setup.supported_vas_services = vas_services;
    evse_setup.enable_certificate_install_service = cert_install;
    evse_setup.dc_limits = dc_limits;
    evse_setup.ac_limits = ac_limits;
    evse_setup.control_mobility_modes = control_mobility_modes;
    evse_setup.powersupply_limits = powersupply_limits;

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};

    GIVEN("forced failed response requested - response code must be FAILED") {
        // This test verifies the centralized forced-failed mechanism in Context.
        // The state itself forwards the generated response through
        // respond_and_publish_response_code(), which applies the override before sending.

        dt::ResponseCode captured_response_code{dt::ResponseCode::OK};

        // Capture the response_code feedback so we can verify it was published correctly
        session::feedback::Callbacks callbacks{};
        callbacks.response_code = [&captured_response_code](const dt::ResponseCode& rc) {
            captured_response_code = rc;
        };

        auto state_helper = FsmStateHelper(session::SessionConfig(evse_setup), pause_ctx, callbacks);
        auto& ctx = state_helper.get_context();
        // Set up a valid DC scheduled session so handle_request() itself would return OK
        ctx.session = d20::Session(d20::SelectedServiceParameters(
            dt::ServiceCategory::DC, dt::DcConnector::Extended, dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing));

        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_ChargeLoop>()};
        ctx.request_forced_failed_response();
        // Build a valid DC_ChargeLoopRequest (would produce OK without forced failed response)
        message_20::DC_ChargeLoopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        auto& req_mode = req.control_mode.emplace<dt::Scheduled_DC_CLReqControlMode>();
        req_mode.target_current = {40, 0};
        req_mode.target_voltage = {400, 0};
        req.meter_info_requested = false;
        req.present_voltage = {330, 0};

        state_helper.handle_request(req);
        fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Response and feedback callback both report FAILED") {
            const auto response_message = ctx.get_response<message_20::DC_ChargeLoopResponse>();
            REQUIRE(response_message.has_value());
            REQUIRE(response_message->response_code >= dt::ResponseCode::FAILED);
            REQUIRE(captured_response_code >= dt::ResponseCode::FAILED);
        }
    }
}
