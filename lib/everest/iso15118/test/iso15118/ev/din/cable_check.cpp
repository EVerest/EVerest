// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/din/state/cable_check.hpp>
#include <iso15118/ev/din/state/charge_parameter_discovery.hpp>
#include <iso15118/message_din/cable_check.hpp>
#include <iso15118/message_din/charge_parameter_discovery.hpp>
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

const auto by_charge_parameter_discovery = entered_by([](DinStateHelper& helper) {
    message_din::ChargeParameterDiscoveryResponse res{};
    res.header = header();
    res.response_code = dt::ResponseCode::OK;
    res.evse_processing = dt::EvseProcessing::Finished;
    res.dc_evse_charge_parameter.emplace().dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
    return successor_of<ev::din::state::ChargeParameterDiscovery>(helper, res);
});

// CableCheck as ChargeParameterDiscovery enters it without CP-state feedback, its first request taken.
ev::din::BasePointerType begin_cable_check(DinStateHelper& helper) {
    auto result = State::begin(helper.get_context());
    REQUIRE(result.output == ev::din::Disposition::Transitioning);
    REQUIRE(helper.get_message_exchange().take_request().has_value());
    return std::move(result.new_state);
}
const auto begun = entered_by(begin_cable_check);

// CableCheck as entered while CP is below state C or D: its first request held.
const auto held = entered_by([](DinStateHelper& helper) {
    auto result = State::begin(helper.get_context());
    REQUIRE(result.output == ev::din::Disposition::Waiting);
    return std::move(result.new_state);
});

// A shutdown tears the session down over the wire: SessionStopReq goes out and the isolation poll
// stops instead of being re-sent.
void expect_diverts_to_session_stop(PrimedState<State>& primed, const message_din::CableCheckResponse& res) {
    primed.handle_response(res);
    const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == StateID::SessionStop);
    const auto requests = primed.take_requests();
    REQUIRE(requests.get<message_din::SessionStopRequest>().has_value());
    REQUIRE(requests.get<message_din::CableCheckRequest>().has_value() == false);
}
} // namespace

SCENARIO("DIN 70121 EV CableCheck's first request goes out on the ChargeParameterDiscovery transition") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed, by_charge_parameter_discovery};

    const auto request = primed.take_requests().get<message_din::CableCheckRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->dc_ev_status.ev_ready == true);
    REQUIRE(request->header.session_id == SESSION_ID);
}

SCENARIO("DIN 70121 EV CableCheck stays and re-polls on Ongoing") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed, begun};

    primed.handle_response(make_response(header(), dt::ResponseCode::OK, dt::EvseProcessing::Ongoing,
                                         dt::DcEvseStatusCode::EVSE_IsolationMonitoringActive));
    const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(result.output == ev::din::Disposition::Awaiting);
    REQUIRE(primed.fsm.get_current_state_id() == StateID::CableCheck);
    REQUIRE(primed.take_requests().get<message_din::CableCheckRequest>().has_value());
}

SCENARIO("DIN 70121 EV CableCheck transitions to PreCharge on Finished") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed, begun};

    primed.handle_response(ok_response(header()));
    const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == StateID::PreCharge);
    REQUIRE(primed.take_requests().get<message_din::PreChargeRequest>().has_value());
}

SCENARIO("DIN 70121 EV CableCheck ignores status code and isolation status on Finished [V2G-DC-893/894]") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed, begun};

    primed.handle_response(make_response(header(), dt::ResponseCode::OK, dt::EvseProcessing::Finished,
                                         dt::DcEvseStatusCode::EVSE_Malfunction, dt::IsolationLevel::Invalid));
    primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(primed.fsm.get_current_state_id() == StateID::PreCharge);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("DIN 70121 EV CableCheck diverts to SessionStop on a shutdown status code") {
    const ev::feedback::Callbacks callbacks{};

    SECTION("EVSE_Shutdown") {
        PrimedState<State> primed{callbacks, no_seed, begun};
        expect_diverts_to_session_stop(primed,
                                       make_response(header(), dt::ResponseCode::OK, dt::EvseProcessing::Finished,
                                                     dt::DcEvseStatusCode::EVSE_Shutdown));
    }
    SECTION("EVSE_EmergencyShutdown") {
        PrimedState<State> primed{callbacks, no_seed, begun};
        expect_diverts_to_session_stop(primed,
                                       make_response(header(), dt::ResponseCode::OK, dt::EvseProcessing::Finished,
                                                     dt::DcEvseStatusCode::EVSE_EmergencyShutdown));
    }
}

SCENARIO("DIN 70121 EV CableCheck terminates on a shutdown status code over a latched EV pause") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed, begun};

    primed.ctx.set_pause_charging_requested(true);
    expect_diverts_to_session_stop(primed, make_response(header(), dt::ResponseCode::OK, dt::EvseProcessing::Finished,
                                                         dt::DcEvseStatusCode::EVSE_Shutdown));

    message_din::SessionStopResponse session_stop{};
    session_stop.header = header();
    session_stop.response_code = dt::ResponseCode::OK;
    primed.handle_response(session_stop);
    primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(primed.ctx.is_session_stopped() == true);
    REQUIRE(primed.ctx.is_session_paused() == false);
}

SCENARIO("DIN 70121 EV CableCheck acts on a shutdown status code while EVSEProcessing is Ongoing") {
    const ev::feedback::Callbacks callbacks{};

    SECTION("EVSE_EmergencyShutdown") {
        PrimedState<State> primed{callbacks, no_seed, begun};
        expect_diverts_to_session_stop(primed,
                                       make_response(header(), dt::ResponseCode::OK, dt::EvseProcessing::Ongoing,
                                                     dt::DcEvseStatusCode::EVSE_EmergencyShutdown));
    }
    SECTION("EVSE_Shutdown") {
        PrimedState<State> primed{callbacks, no_seed, begun};
        expect_diverts_to_session_stop(primed,
                                       make_response(header(), dt::ResponseCode::OK, dt::EvseProcessing::Ongoing,
                                                     dt::DcEvseStatusCode::EVSE_Shutdown));
    }
}

SCENARIO("DIN 70121 EV CableCheck diverts to SessionStop on a latched stop request") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed, begun};

    primed.ctx.set_stop_charging_requested(true);
    primed.handle_response(ok_response(header()));
    primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(primed.fsm.get_current_state_id() == StateID::SessionStop);
    REQUIRE(primed.take_requests().get<message_din::SessionStopRequest>().has_value());
}

SCENARIO("DIN 70121 EV CableCheck holds the first request until CP state C or D [V2G-DC-547]") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, ev::params::EvSessionParams{}, true, no_seed, held};

    REQUIRE(primed.take_requests().empty());
}

SCENARIO("DIN 70121 EV CableCheck sends the first request once CP state C or D is reported") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, ev::params::EvSessionParams{}, true, no_seed, held};

    primed.helper.latch(ev::din::CpState{true});
    const auto result = primed.feed(ev::din::Event::CONTROL_MESSAGE);

    REQUIRE(result.output == ev::din::Disposition::Awaiting);
    REQUIRE(primed.take_requests().get<message_din::CableCheckRequest>().has_value());
}

SCENARIO("DIN 70121 EV CableCheck ignores control messages while it waits for CP state C or D") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, ev::params::EvSessionParams{}, true, no_seed, held};

    primed.helper.latch(ev::din::CpState{false});
    const auto result = primed.feed(ev::din::Event::CONTROL_MESSAGE);

    REQUIRE(result.output == ev::din::Disposition::Ignored);
    REQUIRE(primed.take_requests().empty());
}

SCENARIO("DIN 70121 EV CableCheck's first request goes out at once when CP state C or D is already reported") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_cp_c = [](DinStateHelper& helper) { helper.get_context().set_cp_state(true); };
    PrimedState<State> primed{callbacks, ev::params::EvSessionParams{}, true, seed_cp_c, by_charge_parameter_discovery};

    REQUIRE(primed.take_requests().get<message_din::CableCheckRequest>().has_value());
}

SCENARIO("DIN 70121 EV CableCheck emits no second request on a later control message") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_cp_c = [](DinStateHelper& helper) { helper.get_context().set_cp_state(true); };
    PrimedState<State> primed{callbacks, ev::params::EvSessionParams{}, true, seed_cp_c, by_charge_parameter_discovery};

    REQUIRE(primed.helper.get_message_exchange().take_request().has_value());
    primed.helper.latch(ev::din::CpState{true});
    const auto result = primed.feed(ev::din::Event::CONTROL_MESSAGE);

    REQUIRE(result.output == ev::din::Disposition::Ignored);
    REQUIRE(primed.take_requests().empty());
}

SCENARIO("DIN 70121 EV CableCheck diverts to SessionStop on a stop while it waits for CP state C or D") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, ev::params::EvSessionParams{}, true, no_seed, held};

    primed.helper.latch(ev::din::StopCharging{true});
    const auto result = primed.feed(ev::din::Event::CONTROL_MESSAGE);

    REQUIRE(result.output == ev::din::Disposition::Transitioning);
    REQUIRE(primed.fsm.get_current_state_id() == StateID::SessionStop);
    REQUIRE(primed.take_requests().get<message_din::SessionStopRequest>().has_value());
}

// A held CableCheck has sent nothing, so a response can only be stray: an Ongoing one would re-poll
// and a later CP event would send a second request, a Finished one would reach PreCharge without any
// CableCheckReq.
SCENARIO("DIN 70121 EV CableCheck stops on a response that arrives while it holds its request") {
    const auto processing = GENERATE(dt::EvseProcessing::Ongoing, dt::EvseProcessing::Finished);
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, ev::params::EvSessionParams{}, true, no_seed, held};

    primed.handle_response(make_response(header(), dt::ResponseCode::OK, processing, dt::DcEvseStatusCode::EVSE_Ready));
    const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(result.output == ev::din::Disposition::Stopping);
    REQUIRE(primed.fsm.get_current_state_id() == StateID::CableCheck);
    REQUIRE(primed.ctx.is_session_stopped());
    REQUIRE(primed.take_requests().empty());
}

SCENARIO("DIN 70121 EV CableCheck rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](DinStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(SESSION_ID);
        return fsm::v2::FSM<ev::din::StateBase>{begin_cable_check(helper)};
    };
    message_din::PreChargeResponse wrong;
    wrong.header = header();
    wrong.response_code = dt::ResponseCode::OK;
    check_rejection_paths(callbacks, StateID::CableCheck, make_fsm, ok_response, wrong);
}
