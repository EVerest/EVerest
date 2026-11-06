// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "helper.hpp"

#include <iso15118/ev/d2/state/cable_check.hpp>
#include <iso15118/ev/d2/state/charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/cable_check.hpp>
#include <iso15118/message_2/charge_parameter_discovery.hpp>
#include <iso15118/message_2/session_stop.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

namespace {

message_2::CableCheckResponse make_response(const message_2::Header& header, dt::ResponseCode code,
                                            dt::EVSEProcessing processing,
                                            dt::DC_EVSEStatusCode status = dt::DC_EVSEStatusCode::EVSE_Ready,
                                            std::optional<dt::IsolationLevel> isolation = dt::IsolationLevel::Valid) {
    message_2::CableCheckResponse res;
    res.header = header;
    res.response_code = code;
    res.evse_processing = processing;
    res.dc_evse_status.status_code = status;
    res.dc_evse_status.isolation_status = isolation;
    return res;
}

using ev::d2::state::CableCheck;

const auto by_charge_parameter_discovery = d2_entered_by([](D2StateHelper& helper) {
    message_2::ChargeParameterDiscoveryResponse res{};
    res.header = d2_header();
    res.response_code = dt::ResponseCode::OK;
    res.evse_processing = dt::EVSEProcessing::Finished;
    res.sa_schedule_list.emplace().push_back(dt::SAScheduleTuple{1, {}, std::nullopt});
    return d2_successor_of<ev::d2::state::ChargeParameterDiscovery>(helper, res);
});

// CableCheck as ChargeParameterDiscovery enters it without CP-state feedback, its first request taken.
ev::d2::BasePointerType begin_cable_check(D2StateHelper& helper) {
    auto result = CableCheck::begin(helper.get_context());
    REQUIRE(result.output == ev::d2::Disposition::Transitioning);
    REQUIRE(helper.get_message_exchange().take_request().has_value());
    return std::move(result.new_state);
}
const auto begun = d2_entered_by(begin_cable_check);

// CableCheck as entered while CP is below state C or D: its first request held.
const auto held = d2_entered_by([](D2StateHelper& helper) {
    auto result = CableCheck::begin(helper.get_context());
    REQUIRE(result.output == ev::d2::Disposition::Waiting);
    return std::move(result.new_state);
});

} // namespace

SCENARIO("ISO15118-2 EV CableCheck's first request goes out on the ChargeParameterDiscovery transition") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<CableCheck> primed{callbacks, d2_no_seed, by_charge_parameter_discovery};

    const auto request = primed.take_requests().get<message_2::CableCheckRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->header.session_id == D2_SESSION_ID);
    REQUIRE(request->dc_ev_status.ev_ready == true);
    REQUIRE(request->dc_ev_status.ev_ress_soc == 50);
}

SCENARIO("ISO15118-2 EV CableCheck resends on Ongoing") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<CableCheck> primed{callbacks, d2_no_seed, by_charge_parameter_discovery};

    REQUIRE(primed.helper.get_message_exchange().take_request().has_value());
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSEProcessing::Ongoing));
    const auto result = primed.feed(ev::d2::Event::V2GTP_MESSAGE);

    REQUIRE(result.output == ev::d2::Disposition::Awaiting);
    REQUIRE(primed.take_requests().get<message_2::CableCheckRequest>().has_value());
}

SCENARIO("ISO15118-2 EV CableCheck transitions to PreCharge on Finished") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<CableCheck> primed{callbacks, d2_no_seed, begun};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSEProcessing::Finished));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::PreCharge);
}

SCENARIO("ISO15118-2 EV CableCheck accepts an isolation Warning") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<CableCheck> primed{callbacks, d2_no_seed, begun};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSEProcessing::Finished,
                                         dt::DC_EVSEStatusCode::EVSE_Ready, dt::IsolationLevel::Warning));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::PreCharge);
}

SCENARIO("ISO15118-2 EV CableCheck stops on a failed isolation") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<CableCheck> primed{callbacks, d2_no_seed, begun};

    expect_stops_session(primed,
                         make_response(d2_header(), dt::ResponseCode::OK, dt::EVSEProcessing::Finished,
                                       dt::DC_EVSEStatusCode::EVSE_Ready, dt::IsolationLevel::Fault),
                         ev::d2::StateID::CableCheck);
}

// [V2G2-880] makes every status code without an explicit requirement informational, and [V2G2-525]
// makes PreChargeReq a shall on Finished. Table 98 names EVSE_IsolationMonitoringActive as the state
// a charging station stays in while it checks cable isolation integrity, so it is the value a SECC
// performing a cable check is most likely to report. Only a shutdown stops the session.
SCENARIO("ISO15118-2 EV CableCheck proceeds on an informational status code at Finished") {
    const auto status =
        GENERATE(dt::DC_EVSEStatusCode::EVSE_IsolationMonitoringActive, dt::DC_EVSEStatusCode::EVSE_NotReady,
                 dt::DC_EVSEStatusCode::EVSE_Malfunction, dt::DC_EVSEStatusCode::EVSE_UtilityInterruptEvent);
    const ev::feedback::Callbacks callbacks{};
    PrimedState<CableCheck> primed{callbacks, d2_no_seed, begun};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, dt::EVSEProcessing::Finished, status));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::PreCharge);
}

SCENARIO("ISO15118-2 EV CableCheck stops on a shutdown status code") {
    const auto status = GENERATE(dt::DC_EVSEStatusCode::EVSE_Shutdown, dt::DC_EVSEStatusCode::EVSE_EmergencyShutdown);
    const ev::feedback::Callbacks callbacks{};
    PrimedState<CableCheck> primed{callbacks, d2_no_seed, begun};

    expect_stops_session(primed, make_response(d2_header(), dt::ResponseCode::OK, dt::EVSEProcessing::Finished, status),
                         ev::d2::StateID::CableCheck);
}

// Table 96 marks EVSEIsolationStatus optional and its semantics row delegates the handling to
// IEC CDV 61851-23, so an absent element is permitted on the wire. Stopping here is a deliberate
// deviation towards safety: without a reported isolation result there is nothing to close the
// contactor on. This case pins that choice.
SCENARIO("ISO15118-2 EV CableCheck stops when the EVSE reports no isolation status at all") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<CableCheck> primed{callbacks, d2_no_seed, begun};

    expect_stops_session(primed,
                         make_response(d2_header(), dt::ResponseCode::OK, dt::EVSEProcessing::Finished,
                                       dt::DC_EVSEStatusCode::EVSE_Ready, std::nullopt),
                         ev::d2::StateID::CableCheck);
}

SCENARIO("ISO15118-2 EV CableCheck holds the first request until CP state C or D") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<CableCheck> primed{callbacks, ev::params::EvSessionParams{}, true, d2_no_seed, held};

    REQUIRE(primed.take_requests().empty());

    primed.helper.latch(ev::d20::CpState{true});
    const auto result = primed.feed(ev::d2::Event::CONTROL_MESSAGE);

    REQUIRE(result.output == ev::d2::Disposition::Awaiting);
    REQUIRE(primed.take_requests().get<message_2::CableCheckRequest>().has_value());
}

SCENARIO("ISO15118-2 EV CableCheck ignores control messages while it waits for CP state C or D") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<CableCheck> primed{callbacks, ev::params::EvSessionParams{}, true, d2_no_seed, held};

    primed.helper.latch(ev::d20::CpState{false});
    REQUIRE(primed.feed(ev::d2::Event::CONTROL_MESSAGE).output == ev::d2::Disposition::Ignored);
    REQUIRE(primed.take_requests().empty());
}

SCENARIO("ISO15118-2 EV CableCheck's first request goes out at once when CP state C or D is already reported") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_cp = [](D2StateHelper& helper) { helper.get_context().set_cp_state(true); };
    PrimedState<CableCheck> primed{callbacks, ev::params::EvSessionParams{}, true, seed_cp,
                                   by_charge_parameter_discovery};

    REQUIRE(primed.take_requests().get<message_2::CableCheckRequest>().has_value());
}

SCENARIO("ISO15118-2 EV CableCheck diverts to SessionStop on a stop while it waits for CP state C or D") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<CableCheck> primed{callbacks, ev::params::EvSessionParams{}, true, d2_no_seed, held};

    primed.helper.latch(ev::d20::StopCharging{true});
    const auto result = primed.feed(ev::d2::Event::CONTROL_MESSAGE);

    REQUIRE(result.output == ev::d2::Disposition::Transitioning);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::SessionStop);
    REQUIRE(primed.take_requests().get<message_2::SessionStopRequest>().has_value());
}

// A held CableCheck has sent nothing, so a response can only be stray: an Ongoing one would re-poll
// and a later CP event would send a second request, a Finished one would reach PreCharge without any
// CableCheckReq.
SCENARIO("ISO15118-2 EV CableCheck stops on a response that arrives while it holds its request") {
    const auto processing = GENERATE(dt::EVSEProcessing::Ongoing, dt::EVSEProcessing::Finished);
    const ev::feedback::Callbacks callbacks{};
    PrimedState<CableCheck> primed{callbacks, ev::params::EvSessionParams{}, true, d2_no_seed, held};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, processing));
    const auto result = primed.feed(ev::d2::Event::V2GTP_MESSAGE);

    REQUIRE(result.output == ev::d2::Disposition::Stopping);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::CableCheck);
    REQUIRE(primed.ctx.is_session_stopped());
    REQUIRE(primed.take_requests().empty());
}

SCENARIO("ISO15118-2 EV CableCheck rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](D2StateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(D2_SESSION_ID);
        return fsm::v2::FSM<ev::d2::StateBase>{begin_cable_check(helper)};
    };
    const auto make_ok = [](const message_2::Header& header) {
        return make_response(header, dt::ResponseCode::OK, dt::EVSEProcessing::Finished);
    };
    message_2::AuthorizationResponse wrong;
    wrong.header = d2_header();
    wrong.response_code = dt::ResponseCode::OK;
    wrong.evse_processing = dt::EVSEProcessing::Finished;
    check_rejection_paths(callbacks, ev::d2::StateID::CableCheck, make_fsm, make_ok, wrong);
}
