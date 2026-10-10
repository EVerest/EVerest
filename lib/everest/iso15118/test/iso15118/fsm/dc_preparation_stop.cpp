// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "helper.hpp"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <tuple>
#include <type_traits>

#include <iso15118/d20/state/dc_cable_check.hpp>
#include <iso15118/d20/state/dc_charge_parameter_discovery.hpp>
#include <iso15118/d20/state/dc_pre_charge.hpp>
#include <iso15118/d20/state/schedule_exchange.hpp>
#include <iso15118/message/dc_cable_check.hpp>
#include <iso15118/message/dc_charge_parameter_discovery.hpp>
#include <iso15118/message/dc_pre_charge.hpp>
#include <iso15118/message/schedule_exchange.hpp>
#include <iso15118/message/session_stop.hpp>

using PreCharge =
    std::tuple<d20::state::DC_PreCharge, message_20::DC_PreChargeRequest, message_20::DC_PreChargeResponse>;

using CableCheck =
    std::tuple<d20::state::DC_CableCheck, message_20::DC_CableCheckRequest, message_20::DC_CableCheckResponse>;

using ChargeParameters =
    std::tuple<d20::state::DC_ChargeParameterDiscovery, message_20::DC_ChargeParameterDiscoveryRequest,
               message_20::DC_ChargeParameterDiscoveryResponse>;

using Schedule =
    std::tuple<d20::state::ScheduleExchange, message_20::ScheduleExchangeRequest, message_20::ScheduleExchangeResponse>;

TEMPLATE_TEST_CASE("ISO15118-20 DC preparation stop", "[dc-preparation-stop]", PreCharge, CableCheck, ChargeParameters,
                   Schedule) {
    using State = std::tuple_element_t<0, TestType>;
    using Request = std::tuple_element_t<1, TestType>;
    using Response = std::tuple_element_t<2, TestType>;

    std::optional<d20::PauseContext> pause_ctx;
    unsigned feedback_count = 0;
    session::feedback::Callbacks callbacks{};
    callbacks.signal = [&](auto) { ++feedback_count; };
    callbacks.dc_pre_charge_target_voltage = [&](auto) { ++feedback_count; };
    callbacks.dc_max_limits = [&](const auto&) { ++feedback_count; };
    callbacks.notify_ev_charging_needs = [&](const auto&...) { ++feedback_count; };
    FsmStateHelper helper(session::SessionConfig(create_default_evse_setup()), pause_ctx, callbacks);
    auto& ctx = helper.get_context();
    ctx.session = d20::Session(
        d20::SelectedServiceParameters(dt::ServiceCategory::DC, dt::DcConnector::Extended, dt::ControlMode::Scheduled,
                                       dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing));
    fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<State>()};
    const auto initial_state = fsm.get_current_state_id();

    Request req{};
    req.header.session_id = ctx.session.get_id();
    req.header.timestamp = 1691411798;
    if constexpr (std::is_same_v<TestType, PreCharge>) {
        req.processing = dt::Processing::Finished;
        req.target_voltage = {400, 0};
        helper.set_active_control_event(d20::PresentVoltageCurrent{400.1f, 0.0f});
        fsm.feed(d20::Event::CONTROL_MESSAGE);
    }

    if constexpr (std::is_same_v<TestType, Schedule>) {
        req.max_supporting_points = 12;
        req.control_mode.template emplace<dt::Scheduled_SEReqControlMode>();
    }

    if constexpr (std::is_same_v<TestType, CableCheck> or std::is_same_v<TestType, PreCharge>) {
        const bool already_started = GENERATE(false, true);
        if (already_started) {
            auto ongoing_req = req;
            if constexpr (std::is_same_v<TestType, PreCharge>) {
                ongoing_req.processing = dt::Processing::Ongoing;
            }
            helper.handle_request(ongoing_req);
            REQUIRE_FALSE(fsm.feed(d20::Event::V2GTP_MESSAGE).transitioned());
            const auto res = ctx.get_response<Response>();
            REQUIRE(res.has_value());
            REQUIRE(res->response_code == dt::ResponseCode::OK);
            REQUIRE(feedback_count > 0);
            feedback_count = 0;
        }
    }

    const bool direct_shutdown = GENERATE(false, true);
    if (direct_shutdown) {
        ctx.request_shutdown();
    } else {
        helper.set_active_control_event(d20::StopCharging{true});
        CHECK_FALSE(fsm.feed(d20::Event::CONTROL_MESSAGE).transitioned());
    }
    CHECK(ctx.shutdown_requested());
    const bool send_false = GENERATE(false, true);
    if (send_false) {
        helper.set_active_control_event(d20::StopCharging{false});
        CHECK_FALSE(fsm.feed(d20::Event::CONTROL_MESSAGE).transitioned());
        CHECK(ctx.shutdown_requested());
    }

    SECTION("SessionStopReq still terminates normally") {
        message_20::SessionStopRequest stop_req{};
        stop_req.header = req.header;
        stop_req.charging_session = dt::ChargingSession::Terminate;
        helper.handle_request(stop_req);
        CHECK_FALSE(fsm.feed(d20::Event::V2GTP_MESSAGE).transitioned());
        const auto res = ctx.get_response<message_20::SessionStopResponse>();
        REQUIRE(res.has_value());
        CHECK(res->response_code == dt::ResponseCode::OK);
        CHECK(ctx.session_stopped);
        CHECK(fsm.get_current_state_id() == initial_state);
        CHECK(feedback_count == 0);
    }

    SECTION("Preparation request ends the session") {
        if constexpr (std::is_same_v<TestType, CableCheck>) {
            const bool completed = GENERATE(false, true);
            if (completed) {
                helper.set_active_control_event(d20::CableCheckFinished{true});
                CHECK_FALSE(fsm.feed(d20::Event::CONTROL_MESSAGE).transitioned());
            }
        }
        if constexpr (std::is_same_v<TestType, PreCharge>) {
            req.processing = GENERATE(dt::Processing::Ongoing, dt::Processing::Finished);
        }
        const bool unknown_session = GENERATE(false, true);
        if (unknown_session) {
            req.header.session_id = d20::Session().get_id();
        }
        helper.handle_request(req);
        const auto result = fsm.feed(d20::Event::V2GTP_MESSAGE);
        const auto res = ctx.get_response<Response>();
        REQUIRE(res.has_value());
        CHECK(res->response_code ==
              (unknown_session ? dt::ResponseCode::FAILED_UnknownSession : dt::ResponseCode::FAILED_SequenceError));
        CHECK(res->header.session_id == ctx.session.get_id());
        CHECK(std::get<1>(helper.get_message_exchange().check_and_clear_response()) > 0);
        CHECK(ctx.session_stopped);
        CHECK_FALSE(result.transitioned());
        CHECK(fsm.get_current_state_id() == initial_state);
        CHECK(feedback_count == 0);
        if constexpr (std::is_same_v<TestType, CableCheck>) {
            CHECK(res->processing == (unknown_session ? dt::Processing::Ongoing : dt::Processing::Finished));
        }
        if constexpr (std::is_same_v<TestType, PreCharge>) {
            CHECK(res->present_voltage.value == (unknown_session ? 0 : 4001));
            CHECK(res->present_voltage.exponent == (unknown_session ? 0 : -1));
        }
    }
}

TEMPLATE_TEST_CASE("ISO15118-20 false stop leaves DC preparation running", "[dc-preparation-stop]", PreCharge,
                   CableCheck, ChargeParameters, Schedule) {
    using State = std::tuple_element_t<0, TestType>;
    using Request = std::tuple_element_t<1, TestType>;
    using Response = std::tuple_element_t<2, TestType>;
    std::optional<d20::PauseContext> pause_ctx;
    FsmStateHelper helper(session::SessionConfig(create_default_evse_setup()), pause_ctx, {});
    auto& ctx = helper.get_context();
    ctx.session = d20::Session(
        d20::SelectedServiceParameters(dt::ServiceCategory::DC, dt::DcConnector::Extended, dt::ControlMode::Scheduled,
                                       dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing));
    fsm::v2::FSM<d20::StateBase> fsm{ctx.create_state<State>()};
    helper.set_active_control_event(d20::StopCharging{false});
    REQUIRE_FALSE(fsm.feed(d20::Event::CONTROL_MESSAGE).transitioned());
    REQUIRE_FALSE(ctx.shutdown_requested());

    Request req{};
    req.header.session_id = ctx.session.get_id();
    auto next_state = d20::StateID::ScheduleExchange;
    if constexpr (std::is_same_v<TestType, CableCheck>) {
        helper.set_active_control_event(d20::CableCheckFinished{true});
        fsm.feed(d20::Event::CONTROL_MESSAGE);
        next_state = d20::StateID::DC_PreCharge;
    } else if constexpr (std::is_same_v<TestType, PreCharge>) {
        req.processing = dt::Processing::Finished;
        req.target_voltage = {400, 0};
        next_state = d20::StateID::PowerDelivery;
    } else if constexpr (std::is_same_v<TestType, Schedule>) {
        req.max_supporting_points = 12;
        req.control_mode.template emplace<dt::Scheduled_SEReqControlMode>();
        next_state = d20::StateID::DC_CableCheck;
    }
    helper.handle_request(req);
    CHECK(fsm.feed(d20::Event::V2GTP_MESSAGE).transitioned());
    CHECK(fsm.get_current_state_id() == next_state);
    CHECK_FALSE(ctx.session_stopped);
    const auto res = ctx.get_response<Response>();
    REQUIRE(res.has_value());
    CHECK(res->response_code == dt::ResponseCode::OK);
}
