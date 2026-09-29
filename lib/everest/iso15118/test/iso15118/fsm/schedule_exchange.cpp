// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/d20/state/schedule_exchange.hpp>

#include <iso15118/message/schedule_exchange.hpp>

using namespace iso15118;

namespace dt = message_20::datatypes;

SCENARIO("Schedule Exchange state handling") {

    using Scheduled_ModeReq = message_20::datatypes::Scheduled_SEReqControlMode;
    using Scheduled_ModeRes = message_20::datatypes::Scheduled_SEResControlMode;
    using Dynamic_ModeReq = message_20::datatypes::Dynamic_SEReqControlMode;
    using Dynamic_ModeRes = message_20::datatypes::Dynamic_SEResControlMode;

    auto evse_setup = create_default_evse_setup();
    evse_setup.dc_limits.charge_limits.power.max = {22, 3};

    std::optional<d20::PauseContext> pause_ctx{std::nullopt};
    session::feedback::Callbacks callbacks{};

    auto state_helper = FsmStateHelper(d20::SessionConfig(evse_setup), pause_ctx, callbacks);
    auto& ctx = state_helper.get_context();

    GIVEN("Bad case - Unknown session") {

        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ScheduleExchange>()};

        message_20::ScheduleExchangeRequest req;
        req.header.session_id = d20::Session().get_id();
        req.header.timestamp = 1691411798;

        req.control_mode.emplace<Scheduled_ModeReq>();

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("ResponseCode: FAILED_UnknownSession, mandatory fields should be set") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(ctx.session_stopped == true);

            const auto response_message = ctx.get_response<message_20::ScheduleExchangeResponse>();
            REQUIRE(response_message.has_value());
            const auto& res = response_message.value();

            REQUIRE(res.response_code == dt::ResponseCode::FAILED_UnknownSession);

            REQUIRE(res.processing == dt::Processing::Finished);

            REQUIRE(std::holds_alternative<Dynamic_ModeRes>(res.control_mode) == true);
            const auto& res_control_mode = std::get<Dynamic_ModeRes>(res.control_mode);
            REQUIRE(std::holds_alternative<std::monostate>(res_control_mode.price_schedule) == true);
        }
    }

    GIVEN("Bad case - False control mode") {
        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::DC, dt::DcConnector::Extended, dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing);

        ctx.session = d20::Session(service_parameters);
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ScheduleExchange>()};

        message_20::ScheduleExchangeRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        req.control_mode.emplace<Dynamic_ModeReq>();

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("ResponseCode: FAILED, mandatory fields should be set") {
            REQUIRE(result.transitioned() == false);
            REQUIRE(ctx.session_stopped == true);

            const auto response_message = ctx.get_response<message_20::ScheduleExchangeResponse>();
            REQUIRE(response_message.has_value());
            const auto& res = response_message.value();

            REQUIRE(res.response_code == dt::ResponseCode::FAILED);

            REQUIRE(res.processing == dt::Processing::Finished);

            REQUIRE(std::holds_alternative<Dynamic_ModeRes>(res.control_mode) == true);
            const auto& res_control_mode = std::get<Dynamic_ModeRes>(res.control_mode);
            REQUIRE(std::holds_alternative<std::monostate>(res_control_mode.price_schedule) == true);
        }
    }

    GIVEN("Good case - Scheduled Mode") {
        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::DC, dt::DcConnector::Extended, dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing);

        ctx.session = d20::Session(service_parameters);
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ScheduleExchange>()};

        message_20::ScheduleExchangeRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        req.control_mode.emplace<Scheduled_ModeReq>();

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("ResponseCode: OK") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_CableCheck);

            const auto response_message = ctx.get_response<message_20::ScheduleExchangeResponse>();
            REQUIRE(response_message.has_value());
            const auto& res = response_message.value();

            REQUIRE(res.response_code == dt::ResponseCode::OK);

            REQUIRE(res.processing == dt::Processing::Finished);

            REQUIRE(std::holds_alternative<Scheduled_ModeRes>(res.control_mode) == true);
            const auto& res_control_mode = std::get<Scheduled_ModeRes>(res.control_mode);

            REQUIRE(res_control_mode.schedule_tuple.size() == 1);
            const auto& schedule_tuple = res_control_mode.schedule_tuple[0];

            REQUIRE(dt::from_RationalNumber(schedule_tuple.charging_schedule.power_schedule.entries.at(0).power) ==
                    22000);
        }
    }

    GIVEN("Good case - Dynamic Mode") {
        d20::SelectedServiceParameters service_parameters =
            d20::SelectedServiceParameters(dt::ServiceCategory::DC, dt::DcConnector::Extended, dt::ControlMode::Dynamic,
                                           dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing);

        ctx.session = d20::Session(service_parameters);
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ScheduleExchange>()};

        message_20::ScheduleExchangeRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        req.control_mode.emplace<Dynamic_ModeReq>();

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("ResponseCode: OK") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_CableCheck);

            const auto response_message = ctx.get_response<message_20::ScheduleExchangeResponse>();
            REQUIRE(response_message.has_value());
            const auto& res = response_message.value();

            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.processing == dt::Processing::Finished);

            REQUIRE(std::holds_alternative<Dynamic_ModeRes>(res.control_mode) == true);
        }
    }

    GIVEN("Good case - MCS Dynamic Mode") {
        d20::SelectedServiceParameters service_parameters = d20::SelectedServiceParameters(
            dt::ServiceCategory::MCS, dt::DcConnector::Extended, dt::ControlMode::Dynamic,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing);

        ctx.session = d20::Session(service_parameters);
        fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<d20::state::ScheduleExchange>()};

        message_20::ScheduleExchangeRequest req;
        req.header.session_id = ctx.session.get_id();
        req.header.timestamp = 1691411798;

        req.control_mode.emplace<Dynamic_ModeReq>();

        state_helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);

        THEN("ResponseCode: OK") {
            REQUIRE(result.transitioned() == true);
            REQUIRE(fsm.get_current_state_id() == d20::StateID::DC_CableCheck);

            const auto response_message = ctx.get_response<message_20::ScheduleExchangeResponse>();
            REQUIRE(response_message.has_value());
            const auto& res = response_message.value();

            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.processing == dt::Processing::Finished);

            REQUIRE(std::holds_alternative<Dynamic_ModeRes>(res.control_mode) == true);
        }
    }

    // GIVEN("Good case - setting new departure_time, target_soc & min_soc") // TODO(sl)

    // GIVEN("Bad Case - sequence error") {} // TODO(sl): not here

    // GIVEN("Performance Timeout") {} // TODO(sl): not here

    // GIVEN("Sequence Timeout") {} // TODO(sl): not here
}
