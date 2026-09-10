// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/din/state/cable_check.hpp>
#include <iso15118/message_din/cable_check.hpp>
#include <iso15118/message_din/pre_charge.hpp>
#include <iso15118/message_din/session_stop.hpp>

using namespace iso15118;

namespace {
namespace dt = message_din::datatypes;
using ev::din::StateID;
using State = ev::din::state::CableCheck;

message_din::CableCheckResponse make_response(const message_din::Header& hdr, dt::ResponseCode code,
                                              dt::EvseProcessing processing, dt::DcEvseStatusCode status_code,
                                              dt::IsolationLevel isolation = dt::IsolationLevel::Valid) {
    message_din::CableCheckResponse res;
    res.header = hdr;
    res.response_code = code;
    res.evse_processing = processing;
    res.dc_evse_status.evse_status_code = status_code;
    res.dc_evse_status.evse_isolation_status = isolation;
    return res;
}

message_din::CableCheckResponse ok_response(const message_din::Header& hdr) {
    return make_response(hdr, dt::ResponseCode::OK, dt::EvseProcessing::Finished, dt::DcEvseStatusCode::EVSE_Ready);
}
} // namespace

SCENARIO("DIN 70121 EV CableCheck sends the request with EVReady on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed};

    const auto request = primed.take_requests().get<message_din::CableCheckRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->dc_ev_status.ev_ready == true);
    REQUIRE(request->header.session_id == SESSION_ID);
}

SCENARIO("DIN 70121 EV CableCheck stays and re-polls on Ongoing") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed};

    primed.handle_response(make_response(header(), dt::ResponseCode::OK, dt::EvseProcessing::Ongoing,
                                         dt::DcEvseStatusCode::EVSE_IsolationMonitoringActive));
    const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(result.output == ev::din::Disposition::Awaiting);
    REQUIRE(primed.fsm.get_current_state_id() == StateID::CableCheck);
    REQUIRE(primed.take_requests().get<message_din::CableCheckRequest>().has_value());
}

SCENARIO("DIN 70121 EV CableCheck transitions to PreCharge on Finished") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed};

    primed.handle_response(ok_response(header()));
    const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == StateID::PreCharge);
    REQUIRE(primed.take_requests().get<message_din::PreChargeRequest>().has_value());
}

SCENARIO("DIN 70121 EV CableCheck ignores status code and isolation status on Finished [V2G-DC-893/894]") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed};

    primed.handle_response(make_response(header(), dt::ResponseCode::OK, dt::EvseProcessing::Finished,
                                         dt::DcEvseStatusCode::EVSE_Malfunction, dt::IsolationLevel::Invalid));
    primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(primed.fsm.get_current_state_id() == StateID::PreCharge);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("DIN 70121 EV CableCheck stops the session on a shutdown status code") {
    const ev::feedback::Callbacks callbacks{};

    SECTION("EVSE_Shutdown") {
        PrimedState<State> primed{callbacks, no_seed};
        expect_stops_session(primed,
                             make_response(header(), dt::ResponseCode::OK, dt::EvseProcessing::Finished,
                                           dt::DcEvseStatusCode::EVSE_Shutdown),
                             StateID::CableCheck);
    }
    SECTION("EVSE_EmergencyShutdown") {
        PrimedState<State> primed{callbacks, no_seed};
        expect_stops_session(primed,
                             make_response(header(), dt::ResponseCode::OK, dt::EvseProcessing::Finished,
                                           dt::DcEvseStatusCode::EVSE_EmergencyShutdown),
                             StateID::CableCheck);
    }
}

SCENARIO("DIN 70121 EV CableCheck diverts to SessionStop on a latched stop request") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed};

    primed.ctx.set_stop_charging_requested(true);
    primed.handle_response(ok_response(header()));
    primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(primed.fsm.get_current_state_id() == StateID::SessionStop);
    REQUIRE(primed.take_requests().get<message_din::SessionStopRequest>().has_value());
}

SCENARIO("DIN 70121 EV CableCheck holds the first request until CP state C or D [V2G-DC-547]") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, ev::EvSessionParams{}, true, no_seed};

    REQUIRE(primed.take_requests().empty());
}

SCENARIO("DIN 70121 EV CableCheck sends the first request once CP state C or D is reported") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, ev::EvSessionParams{}, true, no_seed};

    primed.ctx.set_cp_state(true);
    primed.helper.set_control_event(ev::din::CpState{true});
    const auto result = primed.feed(ev::din::Event::CONTROL_MESSAGE);

    REQUIRE(result.output == ev::din::Disposition::Awaiting);
    REQUIRE(primed.take_requests().get<message_din::CableCheckRequest>().has_value());
}

SCENARIO("DIN 70121 EV CableCheck ignores control messages while it waits for CP state C or D") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, ev::EvSessionParams{}, true, no_seed};

    primed.helper.set_control_event(ev::din::CpState{false});
    const auto result = primed.feed(ev::din::Event::CONTROL_MESSAGE);

    REQUIRE(result.output == ev::din::Disposition::Ignored);
    REQUIRE(primed.take_requests().empty());
}

SCENARIO("DIN 70121 EV CableCheck sends on enter when CP state C or D is already reported") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_cp_c = [](DinStateHelper& helper) { helper.get_context().set_cp_state(true); };
    PrimedState<State> primed{callbacks, ev::EvSessionParams{}, true, seed_cp_c};

    REQUIRE(primed.take_requests().get<message_din::CableCheckRequest>().has_value());
}

SCENARIO("DIN 70121 EV CableCheck emits no second request on a later control message") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_cp_c = [](DinStateHelper& helper) { helper.get_context().set_cp_state(true); };
    PrimedState<State> primed{callbacks, ev::EvSessionParams{}, true, seed_cp_c};

    REQUIRE(primed.helper.get_message_exchange().take_request().has_value());
    primed.helper.set_control_event(ev::din::CpState{true});
    const auto result = primed.feed(ev::din::Event::CONTROL_MESSAGE);

    REQUIRE(result.output == ev::din::Disposition::Ignored);
    REQUIRE(primed.take_requests().empty());
}

SCENARIO("DIN 70121 EV CableCheck diverts to SessionStop on a stop while it waits for CP state C or D") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, ev::EvSessionParams{}, true, no_seed};

    primed.ctx.set_stop_charging_requested(true);
    primed.helper.set_control_event(ev::din::StopCharging{true});
    const auto result = primed.feed(ev::din::Event::CONTROL_MESSAGE);

    REQUIRE(result.output == ev::din::Disposition::Transitioning);
    REQUIRE(primed.fsm.get_current_state_id() == StateID::SessionStop);
    REQUIRE(primed.take_requests().get<message_din::SessionStopRequest>().has_value());
}

SCENARIO("DIN 70121 EV CableCheck rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](DinStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(SESSION_ID);
        return fsm::v2::FSM<ev::din::StateBase>{ctx.create_state<State>()};
    };
    message_din::PreChargeResponse wrong;
    wrong.header = header();
    wrong.response_code = dt::ResponseCode::OK;
    check_rejection_paths(callbacks, StateID::CableCheck, make_fsm, ok_response, wrong);
}
