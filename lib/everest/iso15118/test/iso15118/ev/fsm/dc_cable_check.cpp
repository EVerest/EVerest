// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/d20/state/dc_cable_check.hpp>
#include <iso15118/ev/d20/state/dc_pre_charge.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/dc_cable_check.hpp>
#include <iso15118/message/session_stop.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

namespace {
using message_20::datatypes::Processing;
using message_20::datatypes::ResponseCode;

message_20::DC_CableCheckResponse make_response(const message_20::Header& header, ResponseCode code,
                                                Processing processing) {
    return message_20::DC_CableCheckResponse{header, code, processing};
}
} // namespace

SCENARIO("ISO15118-20 EV DC_CableCheck sends initial request on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::DC_CableCheck> primed{callbacks, no_seed};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::DC_CableCheckRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
}

SCENARIO("ISO15118-20 EV DC_CableCheck stays and resends on Ongoing") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::DC_CableCheck> primed{callbacks, no_seed};

    // Consume the request emitted on enter() the way the reactor transmits it before the
    // response arrives, so the post-feed assertion proves feed() emitted a *fresh*
    // DC_CableCheckRequest rather than observing the initial one.
    REQUIRE(primed.helper.get_message_exchange().take_request().has_value());
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
    PrimedState<ev::d20::state::DC_CableCheck> primed{callbacks, no_seed};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, Processing::Finished));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_PreCharge);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV DC_CableCheck stops session on FAILED_UnknownSession") {
    // State-specific rejection beyond the shared triple.
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::DC_CableCheck> primed{callbacks, no_seed};

    expect_stops_session(primed,
                         make_response(SESSION_HEADER, ResponseCode::FAILED_UnknownSession, Processing::Finished),
                         ev::d20::StateID::DC_CableCheck);
}

SCENARIO("ISO15118-20 EV DC_CableCheck rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](FsmStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        return fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<ev::d20::state::DC_CableCheck>()};
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
} // namespace

SCENARIO("ISO15118-20 EV DC_CableCheck holds the first request until CP state C or D") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::DC_CableCheck> primed{callbacks, message_20::datatypes::ServiceCategory::DC,
                                                      with_cp_feedback(), no_seed};

    REQUIRE(primed.take_requests().empty());
}

SCENARIO("ISO15118-20 EV DC_CableCheck sends the first request once CP state C or D is reported") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::DC_CableCheck> primed{callbacks, message_20::datatypes::ServiceCategory::DC,
                                                      with_cp_feedback(), no_seed};

    primed.ctx.set_cp_state(true);
    primed.helper.set_control_event(ev::d20::CpState{true});
    const auto result = primed.feed(ev::d20::Event::CONTROL_MESSAGE);

    REQUIRE(result.output == ev::d20::Disposition::Awaiting);
    REQUIRE(primed.ctx.is_session_stopped() == false);
    const auto requests = primed.take_requests();
    REQUIRE(requests.get<message_20::DC_CableCheckRequest>().has_value());
}

SCENARIO("ISO15118-20 EV DC_CableCheck ignores control messages while it waits for CP state C or D") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::DC_CableCheck> primed{callbacks, message_20::datatypes::ServiceCategory::DC,
                                                      with_cp_feedback(), no_seed};

    primed.helper.set_control_event(ev::d20::CpState{false});
    const auto result = primed.feed(ev::d20::Event::CONTROL_MESSAGE);

    REQUIRE(result.output == ev::d20::Disposition::Ignored);
    REQUIRE(primed.take_requests().empty());
}

SCENARIO("ISO15118-20 EV DC_CableCheck sends on enter when CP state C or D is already reported") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_cp_c = [](FsmStateHelper& helper) { helper.get_context().set_cp_state(true); };
    PrimedState<ev::d20::state::DC_CableCheck> primed{callbacks, message_20::datatypes::ServiceCategory::DC,
                                                      with_cp_feedback(), seed_cp_c};

    REQUIRE(primed.take_requests().get<message_20::DC_CableCheckRequest>().has_value());
}

SCENARIO("ISO15118-20 EV DC_CableCheck emits no second request on a later control message") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_cp_c = [](FsmStateHelper& helper) { helper.get_context().set_cp_state(true); };
    PrimedState<ev::d20::state::DC_CableCheck> primed{callbacks, message_20::datatypes::ServiceCategory::DC,
                                                      with_cp_feedback(), seed_cp_c};

    REQUIRE(primed.helper.get_message_exchange().take_request().has_value());
    primed.helper.set_control_event(ev::d20::CpState{true});
    const auto result = primed.feed(ev::d20::Event::CONTROL_MESSAGE);

    REQUIRE(result.output == ev::d20::Disposition::Ignored);
    REQUIRE(primed.take_requests().empty());
}

SCENARIO("ISO15118-20 EV DC_CableCheck diverts to SessionStop on a stop while it waits for CP state C or D") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::DC_CableCheck> primed{callbacks, message_20::datatypes::ServiceCategory::DC,
                                                      with_cp_feedback(), no_seed};

    primed.ctx.set_stop_charging_requested(true);
    primed.helper.set_control_event(ev::d20::StopCharging{true});
    const auto result = primed.feed(ev::d20::Event::CONTROL_MESSAGE);

    REQUIRE(result.output == ev::d20::Disposition::Transitioning);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::SessionStop);
    REQUIRE(primed.take_requests().get<message_20::SessionStopRequest>().has_value());
}
