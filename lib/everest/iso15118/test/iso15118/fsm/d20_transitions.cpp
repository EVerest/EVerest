// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d20/state/dc_charge_loop.hpp>
#include <iso15118/d20/state/session_setup.hpp>
#include <iso15118/d20/state/supported_app_protocol.hpp>

#include <iso15118/message/dc_charge_loop.hpp>
#include <iso15118/message/supported_app_protocol.hpp>

using namespace iso15118;

SCENARIO("ISO15118-20 supported app protocol state transitions") {

    // Move to helper function?
    const auto evse_id = std::string("everest se");
    const std::vector<message_20::datatypes::ServiceCategory> supported_energy_services = {
        message_20::datatypes::ServiceCategory::DC};
    const std::vector<uint16_t> vas_services{};
    const auto cert_install{false};
    const std::vector<message_20::datatypes::Authorization> auth_services = {message_20::datatypes::Authorization::EIM};
    const d20::DcTransferLimits dc_limits;
    const d20::AcTransferLimits ac_limits;
    // const d20::IecDerTransferLimits der_limits;
    const d20::DcTransferLimits powersupply_limits;
    const std::vector<d20::ControlMobilityNeedsModes> control_mobility_modes = {
        {message_20::datatypes::ControlMode::Scheduled, message_20::datatypes::MobilityNeedsMode::ProvidedByEvcc}};
    const std::string custom_namespace = "urn:iso:std:iso:15118:-20:AABB";

    const d20::EvseSetupConfig evse_setup{evse_id,
                                          supported_energy_services,
                                          auth_services,
                                          vas_services,
                                          cert_install,
                                          dc_limits,
                                          ac_limits,
                                          std::nullopt,
                                          control_mobility_modes,
                                          custom_namespace,
                                          std::nullopt,
                                          std::nullopt,
                                          std::nullopt,
                                          powersupply_limits,
                                          false};

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};

    const session::feedback::Callbacks callbacks{};

    auto state_helper = FsmStateHelper(d20::SessionConfig(evse_setup), pause_ctx, callbacks);
    auto ctx = state_helper.get_context();

    GIVEN("Good case - DC") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::SupportedAppProtocol>()};

        message_20::SupportedAppProtocolRequest req;
        auto& ap = req.app_protocol.emplace_back();
        ap.priority = 1;
        ap.protocol_namespace = "urn:iso:std:iso:15118:-20:DC";
        ap.schema_id = 1;
        ap.version_number_major = 1;
        ap.version_number_minor = 0;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::SessionSetup);

            const auto response_message = ctx.get_response<message_20::SupportedAppProtocolResponse>();
            REQUIRE(response_message.has_value());

            const auto& supported_app_res = response_message.value();

            REQUIRE(supported_app_res.response_code ==
                    message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation);
            REQUIRE(supported_app_res.schema_id.value_or(0) == 1);
        }
    }

    GIVEN("Good case - Custom") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::SupportedAppProtocol>()};

        message_20::SupportedAppProtocolRequest req;

        auto& ap = req.app_protocol.emplace_back();
        ap.priority = 1;
        ap.protocol_namespace = "urn:iso:std:iso:15118:-20:AABB";
        ap.schema_id = 1;
        ap.version_number_major = 1;
        ap.version_number_minor = 0;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::SessionSetup);

            const auto response_message = ctx.get_response<message_20::SupportedAppProtocolResponse>();
            REQUIRE(response_message.has_value());

            const auto& supported_app_res = response_message.value();

            REQUIRE(supported_app_res.response_code ==
                    message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation);
            REQUIRE(supported_app_res.schema_id.value_or(0) == 1);
        }
    }

    GIVEN("Good case - Priority") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::SupportedAppProtocol>()};

        message_20::SupportedAppProtocolRequest req;
        auto& ap_dc = req.app_protocol.emplace_back();
        ap_dc.priority = 2;
        ap_dc.protocol_namespace = "urn:iso:std:iso:15118:-20:DC";
        ap_dc.schema_id = 1;
        ap_dc.version_number_major = 1;
        ap_dc.version_number_minor = 0;

        auto& ap_custom = req.app_protocol.emplace_back();
        ap_custom.priority = 1;
        ap_custom.protocol_namespace = "urn:iso:std:iso:15118:-20:AC";
        ap_custom.schema_id = 3;
        ap_custom.version_number_major = 1;
        ap_custom.version_number_minor = 0;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::SessionSetup);

            const auto response_message = ctx.get_response<message_20::SupportedAppProtocolResponse>();
            REQUIRE(response_message.has_value());

            const auto& supported_app_res = response_message.value();

            REQUIRE(supported_app_res.response_code ==
                    message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation);
            REQUIRE(supported_app_res.schema_id.value_or(0) == 3);
        }
    }

    GIVEN("Bad case - Unknown protocol namespace") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::SupportedAppProtocol>()};

        message_20::SupportedAppProtocolRequest req;
        auto& ap = req.app_protocol.emplace_back();
        ap.priority = 1;
        ap.protocol_namespace = "Foobar";
        ap.schema_id = 12;
        ap.version_number_major = 2;
        ap.version_number_minor = 11;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::SupportedAppProtocol);

            const auto response_message = ctx.get_response<message_20::SupportedAppProtocolResponse>();
            REQUIRE(response_message.has_value());

            const auto& supported_app_res = response_message.value();

            REQUIRE(supported_app_res.response_code ==
                    message_20::SupportedAppProtocolResponse::ResponseCode::Failed_NoNegotiation);
        }
    }

    GIVEN("Bad case - empty app protocol") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::SupportedAppProtocol>()};

        message_20::SupportedAppProtocolRequest req;
        req.app_protocol.emplace_back();

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::SupportedAppProtocol);

            const auto response_message = ctx.get_response<message_20::SupportedAppProtocolResponse>();
            REQUIRE(response_message.has_value());

            const auto& supported_app_res = response_message.value();

            REQUIRE(supported_app_res.response_code ==
                    message_20::SupportedAppProtocolResponse::ResponseCode::Failed_NoNegotiation);
        }
    }

    GIVEN("Good case - Selecting namespace based on supported energy services") {
        ctx.session_config.selecting_sap_based_on_energy_service = true;

        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::SupportedAppProtocol>()};

        message_20::SupportedAppProtocolRequest req;
        auto& ap_dc = req.app_protocol.emplace_back();
        ap_dc.priority = 2;
        ap_dc.protocol_namespace = "urn:iso:std:iso:15118:-20:DC";
        ap_dc.schema_id = 2;
        ap_dc.version_number_major = 1;
        ap_dc.version_number_minor = 0;

        auto& ap_ac = req.app_protocol.emplace_back();
        ap_ac.priority = 1;
        ap_ac.protocol_namespace = "urn:iso:std:iso:15118:-20:AC";
        ap_ac.schema_id = 1;
        ap_ac.version_number_major = 1;
        ap_ac.version_number_minor = 0;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::SessionSetup);

            const auto response_message = ctx.get_response<message_20::SupportedAppProtocolResponse>();
            REQUIRE(response_message.has_value());

            const auto& supported_app_res = response_message.value();

            REQUIRE(supported_app_res.response_code ==
                    message_20::SupportedAppProtocolResponse::ResponseCode::OK_SuccessfulNegotiation);
            REQUIRE(supported_app_res.schema_id.value_or(0) == 2);
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

    const d20::EvseSetupConfig evse_setup{
        evse_id,   supported_energy_services, auth_services, vas_services, cert_install, dc_limits,
        ac_limits, std::nullopt, control_mobility_modes, std::nullopt, std::nullopt, std::nullopt,
        std::nullopt, powersupply_limits};

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

        auto state_helper = FsmStateHelper(d20::SessionConfig(evse_setup), pause_ctx, callbacks);
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
