// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d20/state/dc_pre_charge.hpp>
#include <iso15118/d20/state/power_delivery.hpp>

#include <iso15118/message/dc_pre_charge.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/session_stop.hpp>

using namespace iso15118;

namespace dt = message_20::datatypes;

SCENARIO("ISO15118-20 dc pre charge state transitions") {

    const session::EvseSetupConfig evse_setup = create_default_evse_setup();
    std::optional<d20::PauseContext> pause_ctx{std::nullopt};
    session::feedback::Callbacks callbacks{};

    auto state_helper = FsmStateHelper(session::SessionConfig(evse_setup), pause_ctx, callbacks);
    auto ctx = state_helper.get_context();
    ctx.session = d20::Session();

    GIVEN("Bad Case - Unknown session") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_PreCharge>()};

        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::DC};
        ctx.session_config.supported_vas_services = {};
        ctx.session_ev_info.ev_energy_services = {};

        message_20::DC_PreChargeRequest req;
        req.header.session_id = d20::Session().get_id();
        req.header.timestamp = 1691411798;
        req.processing = dt::Processing::Ongoing;
        req.present_voltage = {0, 0};
        req.target_voltage = {400, 0};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_PreCharge);
            REQUIRE(ctx.session_stopped == true);

            const auto response_message = ctx.get_response<message_20::DC_PreChargeResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_UnknownSession);
            REQUIRE(res.present_voltage.value == 0);
            REQUIRE(res.present_voltage.exponent == 0);
        }
    }

    GIVEN("Good Case - Ongoing") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_PreCharge>()};

        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::DC};

        // Set control event for present voltage (400.1V)
        state_helper.set_active_control_event(d20::PresentVoltageCurrent{400.1f, 0.0f});
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::DC_PreChargeRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.processing = dt::Processing::Ongoing;
        req.present_voltage = {0, 0};
        req.target_voltage = {400, 0};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_PreCharge);

            const auto response_message = ctx.get_response<message_20::DC_PreChargeResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            // 400.1 = 4001 * 10^-1
            REQUIRE(res.present_voltage.value == 4001);
            REQUIRE(res.present_voltage.exponent == -1);
        }
    }

    GIVEN("Good Case - Finished") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_PreCharge>()};

        ctx.session_config.supported_energy_transfer_services = {dt::ServiceCategory::DC};

        // Set control event for present voltage (400.1V)
        state_helper.set_active_control_event(d20::PresentVoltageCurrent{400.1f, 0.0f});
        fsm.feed(d20::Event::CONTROL_MESSAGE);

        message_20::DC_PreChargeRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.processing = dt::Processing::Finished;
        req.present_voltage = {0, 0};
        req.target_voltage = {400, 0};

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition and response") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::PowerDelivery);

            const auto response_message = ctx.get_response<message_20::DC_PreChargeResponse>();
            REQUIRE(response_message.has_value());

            const auto& res = response_message.value();
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            // 400.1 = 4001 * 10^-1
            REQUIRE(res.present_voltage.value == 4001);
            REQUIRE(res.present_voltage.exponent == -1);
        }
    }

    GIVEN("Event then other V2GTP_MESSAGE") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_PreCharge>()};
        const auto result = fsm.feed(d20::Event::FAILED);
        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_PreCharge);
        }
    }

    GIVEN("SessionStopReq") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_PreCharge>()};

        ctx.session_config.cert_install_service = false;
        ctx.session_config.authorization_services = {dt::Authorization::EIM};

        message_20::SessionStopRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.charging_session = dt::ChargingSession::Terminate;

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_PreCharge);

            const auto response_message = ctx.get_response<message_20::SessionStopResponse>();
            REQUIRE(response_message.has_value());

            const auto& session_stop_res = response_message.value();
            REQUIRE(session_stop_res.response_code == dt::ResponseCode::OK);
        }
    }

    GIVEN("Sequence Error") {
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::DC_PreCharge>()};

        ctx.session_config.cert_install_service = false;
        ctx.session_config.authorization_services = {dt::Authorization::EIM};

        message_20::SessionSetupRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;
        req.evccid = "WMIV1234567890ABCDEX";

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("Check state transition") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_PreCharge);

            const auto response_message = ctx.get_response<message_20::SessionSetupResponse>();
            REQUIRE(response_message.has_value());

            const auto& session_setup_res = response_message.value();
            REQUIRE(session_setup_res.response_code == dt::ResponseCode::FAILED_SequenceError);
        }
    }
}
