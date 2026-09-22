// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/d20/state/dc_cable_check.hpp>
#include <iso15118/ev/d20/state/dc_pre_charge.hpp>
#include <iso15118/ev/d20/state/schedule_exchange.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/dc_cable_check.hpp>
#include <iso15118/message/schedule_exchange.hpp>
#include <iso15118/message/session_stop.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

namespace {
using message_20::datatypes::Processing;
using message_20::datatypes::ResponseCode;
using CableCheck = ev::d20::state::DC_CableCheck;

message_20::DC_CableCheckResponse make_response(const message_20::Header& header, ResponseCode code,
                                                Processing processing) {
    return message_20::DC_CableCheckResponse{header, code, processing};
}

// DC_CableCheck as ScheduleExchange enters it without CP-state feedback, its first request taken.
ev::d20::BasePointerType begin_cable_check(FsmStateHelper& helper) {
    auto result = CableCheck::begin(helper.get_context());
    REQUIRE(result.output == ev::d20::Disposition::Transitioning);
    REQUIRE(take_all_requests(helper.get_message_exchange()).get<message_20::DC_CableCheckRequest>().has_value());
    return std::move(result.new_state);
}
const auto begun = entered_by(begin_cable_check);
} // namespace

SCENARIO("ISO15118-20 EV DC_CableCheck stays and resends on Ongoing") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<CableCheck> primed{callbacks, no_seed, begun};

    // The first request is taken, so a request pending after the feed is the one the state sent on Ongoing.
    REQUIRE_FALSE(primed.helper.get_message_exchange().has_request());

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, Processing::Ongoing));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_CableCheck);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::DC_CableCheckRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
}

SCENARIO("ISO15118-20 EV DC_CableCheck transitions to DC_PreCharge on Finished") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<CableCheck> primed{callbacks, no_seed, begun};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, Processing::Finished));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_PreCharge);
    REQUIRE(primed.ctx.is_session_stopped() == false);
    REQUIRE(primed.take_requests().get<message_20::DC_PreChargeRequest>().has_value());
}

SCENARIO("ISO15118-20 EV DC_CableCheck stops session on FAILED_UnknownSession") {
    // State-specific rejection beyond the shared triple.
    const ev::feedback::Callbacks callbacks{};
    PrimedState<CableCheck> primed{callbacks, no_seed, begun};

    expect_stops_session(primed,
                         make_response(SESSION_HEADER, ResponseCode::FAILED_UnknownSession, Processing::Finished),
                         ev::d20::StateID::DC_CableCheck);
}

SCENARIO("ISO15118-20 EV DC_CableCheck rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](FsmStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        return fsm::v2::FSM<ev::d20::StateBase>{begin_cable_check(helper)};
    };
    const auto make_ok = [](const message_20::Header& header) {
        return make_response(header, ResponseCode::OK, Processing::Finished);
    };
    const auto wrong = message_20::AuthorizationResponse{SESSION_HEADER, ResponseCode::OK, Processing::Finished};
    check_rejection_paths(callbacks, ev::d20::StateID::DC_CableCheck, make_fsm, make_ok, wrong);
}

namespace {
ev::d20::SessionOptions with_cp_feedback() {
    ev::d20::SessionOptions options{};
    options.has_cp_state_feedback = true;
    return options;
}

// A DC ScheduleExchange about to be answered Finished: the way a session reaches DC_CableCheck.
template <typename Seed> PrimedState<ev::d20::state::ScheduleExchange> before_cable_check(Seed seed) {
    static const ev::feedback::Callbacks callbacks{};
    return {callbacks, message_20::datatypes::ServiceCategory::DC, with_cp_feedback(), seed};
}

template <typename Primed> auto finish_schedule_exchange(Primed& primed) {
    message_20::ScheduleExchangeResponse res{};
    res.header = SESSION_HEADER;
    res.response_code = ResponseCode::OK;
    res.processing = Processing::Finished;
    primed.handle_response(res);
    return primed.feed(ev::d20::Event::V2GTP_MESSAGE);
}

const auto seed_cp_c = [](FsmStateHelper& helper) { helper.get_context().set_cp_state(true); };
} // namespace

SCENARIO("ISO15118-20 EV ScheduleExchange enters DC_CableCheck Waiting while CP is below state C or D") {
    auto primed = before_cable_check(no_seed);

    const auto result = finish_schedule_exchange(primed);

    REQUIRE(result.output == ev::d20::Disposition::Waiting);
    REQUIRE(result.transitioned());
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_CableCheck);
    REQUIRE(primed.take_requests().empty());
}

SCENARIO("ISO15118-20 EV DC_CableCheck sends the first request once CP state C or D is reported") {
    auto primed = before_cable_check(no_seed);
    finish_schedule_exchange(primed);
    REQUIRE(primed.take_requests().empty());

    primed.helper.latch(ev::d20::CpState{true});
    const auto result = primed.feed(ev::d20::Event::CONTROL_MESSAGE);

    REQUIRE(result.output == ev::d20::Disposition::Awaiting);
    REQUIRE(primed.ctx.is_session_stopped() == false);
    const auto requests = primed.take_requests();
    REQUIRE(requests.get<message_20::DC_CableCheckRequest>().has_value());
}

SCENARIO("ISO15118-20 EV DC_CableCheck ignores control messages while it waits for CP state C or D") {
    auto primed = before_cable_check(no_seed);
    finish_schedule_exchange(primed);

    primed.helper.latch(ev::d20::CpState{false});
    const auto result = primed.feed(ev::d20::Event::CONTROL_MESSAGE);

    REQUIRE(result.output == ev::d20::Disposition::Ignored);
    REQUIRE(primed.take_requests().empty());
}

SCENARIO("ISO15118-20 EV ScheduleExchange sends DC_CableCheckReq when CP state C or D is already reported") {
    auto primed = before_cable_check(seed_cp_c);

    const auto result = finish_schedule_exchange(primed);

    REQUIRE(result.output == ev::d20::Disposition::Transitioning);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_CableCheck);
    REQUIRE(primed.take_requests().get<message_20::DC_CableCheckRequest>().has_value());
}

SCENARIO("ISO15118-20 EV DC_CableCheck emits no second request on a later control message") {
    auto primed = before_cable_check(seed_cp_c);
    finish_schedule_exchange(primed);
    REQUIRE(primed.helper.get_message_exchange().take_request().has_value());

    primed.helper.latch(ev::d20::CpState{true});
    const auto result = primed.feed(ev::d20::Event::CONTROL_MESSAGE);

    REQUIRE(result.output == ev::d20::Disposition::Ignored);
    REQUIRE(primed.take_requests().empty());
}

SCENARIO("ISO15118-20 EV DC_CableCheck diverts to SessionStop on a stop or pause while it waits for CP state C or D") {
    auto primed = before_cable_check(no_seed);
    finish_schedule_exchange(primed);

    const bool stop = GENERATE(true, false);
    if (stop) {
        primed.helper.latch(ev::d20::StopCharging{true});
    } else {
        primed.helper.latch(ev::d20::PauseCharging{true});
    }
    const auto result = primed.feed(ev::d20::Event::CONTROL_MESSAGE);

    REQUIRE(result.output == ev::d20::Disposition::Transitioning);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::SessionStop);
    const auto requests = primed.take_requests();
    REQUIRE_FALSE(requests.get<message_20::DC_CableCheckRequest>().has_value());
    const auto session_stop = requests.get<message_20::SessionStopRequest>();
    REQUIRE(session_stop.has_value());
    // [V2G20-2644]: before PowerDeliveryReq(Start) a pause terminates too.
    REQUIRE(session_stop->charging_session == message_20::datatypes::ChargingSession::Terminate);
}

SCENARIO("ISO15118-20 EV ScheduleExchange diverts to SessionStop on a stop or pause latched before CableCheck") {
    auto primed = before_cable_check(no_seed);
    const bool stop = GENERATE(true, false);
    if (stop) {
        primed.ctx.set_stop_charging_requested(true);
    } else {
        primed.ctx.set_pause_charging_requested(true);
    }

    const auto result = finish_schedule_exchange(primed);

    REQUIRE(result.output == ev::d20::Disposition::Transitioning);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::SessionStop);
    const auto requests = primed.take_requests();
    REQUIRE_FALSE(requests.get<message_20::DC_CableCheckRequest>().has_value());
    const auto session_stop = requests.get<message_20::SessionStopRequest>();
    REQUIRE(session_stop.has_value());
    REQUIRE(session_stop->charging_session == message_20::datatypes::ChargingSession::Terminate);
}

SCENARIO("ISO15118-20 EV DC_CableCheck stops the session on a response while it waits for CP state C or D") {
    auto primed = before_cable_check(no_seed);
    finish_schedule_exchange(primed);
    REQUIRE(primed.take_requests().empty());

    const auto processing = GENERATE(Processing::Ongoing, Processing::Finished);
    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, processing));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.output == ev::d20::Disposition::Stopping);
    REQUIRE(primed.ctx.is_session_stopped());
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_CableCheck);
    REQUIRE(primed.take_requests().empty());
}
