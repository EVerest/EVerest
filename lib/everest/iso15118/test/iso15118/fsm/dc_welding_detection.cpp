// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d20/state/dc_welding_detection.hpp>
#include <iso15118/d20/state/session_stop.hpp>

#include <iso15118/d20/config.hpp>
#include <iso15118/message/dc_welding_detection.hpp>
#include <iso15118/message/session_stop.hpp>

using namespace iso15118;

namespace dt = message_20::datatypes;

SCENARIO("ISO15118-20 dc welding detection state transitions") {

    const auto evse_id = std::string("everest se");
    const std::vector<dt::ServiceCategory> supported_energy_services = {dt::ServiceCategory::DC};
    const auto cert_install = false;
    const std::vector<dt::Authorization> auth_services = {dt::Authorization::EIM};
    const std::vector<uint16_t> vas_services{};

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
        evse_id, supported_energy_services, auth_services, vas_services, cert_install, dc_limits,
        ac_limits, control_mobility_modes, std::nullopt, std::nullopt, std::nullopt, powersupply_limits};

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};
    session::feedback::Callbacks callbacks{};

    auto state_helper = FsmStateHelper(d20::SessionConfig(evse_setup), pause_ctx, callbacks);
    auto ctx = state_helper.get_context();

    GIVEN("Bad case - Unknown session") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_WeldingDetection>()};

        ctx.session_config.supported_energy_transfer_services = supported_energy_services;
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        message_20::DC_WeldingDetectionRequest req;
        req.header.session_id = d20::Session().get_id();
        req.header.timestamp = 1691411798;
        req.processing = dt::Processing::Ongoing;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_WeldingDetection);
            REQUIRE(ctx.session_stopped == true);

            const auto response_message = ctx.get_response<message_20::DC_WeldingDetectionResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_UnknownSession);
            REQUIRE(res.present_voltage.value == 0);
            REQUIRE(res.present_voltage.exponent == 0);
        }
    }

    GIVEN("Good Case") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_WeldingDetection>()};

        ctx.session = d20::Session();

        // Set control event for present voltage
        state_helper.set_active_control_event(d20::PresentVoltageCurrent{200.0f, 0.0f});
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::DC_WeldingDetectionRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.processing = dt::Processing::Ongoing;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_WeldingDetection);

            const auto response_message = ctx.get_response<message_20::DC_WeldingDetectionResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.present_voltage.value == 2000);
            REQUIRE(res.present_voltage.exponent == -1);
        }
    }
}
