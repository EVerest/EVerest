// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d2/state/welding_detection.hpp>
#include <iso15118/ev/detail/d2/state/welding_detection.hpp>
#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/session_stop.hpp>
#include <iso15118/message_2/welding_detection.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

namespace {

message_2::WeldingDetectionResponse make_response(const message_2::Header& header, dt::ResponseCode code,
                                                  double present_voltage) {
    message_2::WeldingDetectionResponse res;
    res.header = header;
    res.response_code = code;
    res.evse_present_voltage = dt::to_physical_value(present_voltage, dt::Unit::V);
    return res;
}

} // namespace

SCENARIO("ISO15118-2 EV WeldingDetection sends the request on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::WeldingDetection> primed{callbacks, d2_no_seed};

    const auto request = primed.take_requests().get<message_2::WeldingDetectionRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->header.session_id == D2_SESSION_ID);
    REQUIRE(request->dc_ev_status.ev_ready == false);
}

SCENARIO("ISO15118-2 EV WeldingDetection moves to SessionStop once the voltage is safe") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::WeldingDetection> primed{callbacks, d2_no_seed};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, 10.0));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::SessionStop);
    REQUIRE(primed.take_requests().get<message_2::SessionStopRequest>().has_value());
}

SCENARIO("ISO15118-2 EV WeldingDetection polls while the voltage is unsafe") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::WeldingDetection> primed{callbacks, d2_no_seed};

    for (int cycle = 0; cycle < ev::d2::state::welding_detection::CYCLES - 1; ++cycle) {
        REQUIRE(primed.helper.get_message_exchange().take_request().has_value());
        primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, 400.0));
        REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).output == ev::d2::Disposition::Awaiting);
    }

    // The cycle backstop ends the loop even at an unsafe voltage.
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, 400.0));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::SessionStop);
}

SCENARIO("ISO15118-2 EV WeldingDetection rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](D2StateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(D2_SESSION_ID);
        return fsm::v2::FSM<ev::d2::StateBase>{ctx.create_state<ev::d2::state::WeldingDetection>()};
    };
    const auto make_ok = [](const message_2::Header& header) {
        return make_response(header, dt::ResponseCode::OK, 10.0);
    };
    message_2::AuthorizationResponse wrong;
    wrong.header = d2_header();
    wrong.response_code = dt::ResponseCode::OK;
    wrong.evse_processing = dt::EVSEProcessing::Finished;
    check_rejection_paths(callbacks, ev::d2::StateID::WeldingDetection, make_fsm, make_ok, wrong);
}
