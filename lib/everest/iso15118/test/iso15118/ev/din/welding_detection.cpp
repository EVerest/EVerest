// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/din/state/welding_detection.hpp>
#include <iso15118/message_din/session_stop.hpp>
#include <iso15118/message_din/welding_detection.hpp>

using namespace iso15118;

namespace {
namespace dt = message_din::datatypes;
using ev::din::StateID;
using State = ev::din::state::WeldingDetection;

message_din::WeldingDetectionResponse make_response(const message_din::Header& hdr, dt::ResponseCode code,
                                                    double present_voltage) {
    message_din::WeldingDetectionResponse res;
    res.header = hdr;
    res.response_code = code;
    res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
    res.evse_present_voltage = present_voltage;
    return res;
}

message_din::WeldingDetectionResponse ok_response(const message_din::Header& hdr) {
    return make_response(hdr, dt::ResponseCode::OK, 0.0);
}
} // namespace

SCENARIO("DIN 70121 EV WeldingDetection sends the request with EVReady cleared on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed};

    const auto request = primed.take_requests().get<message_din::WeldingDetectionRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->dc_ev_status.ev_ready == false);
}

SCENARIO("DIN 70121 EV WeldingDetection transitions to SessionStop on a safe voltage") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed};

    primed.handle_response(make_response(header(), dt::ResponseCode::OK, 20.0));
    const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == StateID::SessionStop);
    REQUIRE(primed.take_requests().get<message_din::SessionStopRequest>().has_value());
}

SCENARIO("DIN 70121 EV WeldingDetection re-polls an unsafe voltage until the cycle backstop") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed};

    for (int cycle = 0; cycle < 2; ++cycle) {
        primed.handle_response(make_response(header(), dt::ResponseCode::OK, 400.0));
        const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);
        REQUIRE(result.output == ev::din::Disposition::Awaiting);
        REQUIRE(primed.fsm.get_current_state_id() == StateID::WeldingDetection);
        REQUIRE(primed.take_requests().get<message_din::WeldingDetectionRequest>().has_value());
    }

    primed.handle_response(make_response(header(), dt::ResponseCode::OK, 400.0));
    primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(primed.fsm.get_current_state_id() == StateID::SessionStop);
    REQUIRE(primed.take_requests().get<message_din::SessionStopRequest>().has_value());
}

SCENARIO("DIN 70121 EV WeldingDetection rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](DinStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(SESSION_ID);
        return fsm::v2::FSM<ev::din::StateBase>{ctx.create_state<State>()};
    };
    message_din::SessionStopResponse wrong;
    wrong.header = header();
    wrong.response_code = dt::ResponseCode::OK;
    check_rejection_paths(callbacks, StateID::WeldingDetection, make_fsm, ok_response, wrong);
}
