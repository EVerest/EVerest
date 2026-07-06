// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/state/dc_welding_detection.hpp>
#include <iso15118/ev/d20/state/session_stop.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/dc_welding_detection.hpp>
#include <iso15118/message/session_stop.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

namespace {
using message_20::datatypes::ResponseCode;

message_20::DC_WeldingDetectionResponse make_response(const message_20::Header& header, ResponseCode code) {
    return message_20::DC_WeldingDetectionResponse{header, code, message_20::datatypes::RationalNumber{0, 0}};
}
} // namespace

SCENARIO("ISO15118-20 EV DC_WeldingDetection sends Starting request on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::DC_WeldingDetection> primed{callbacks, no_seed};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::DC_WeldingDetectionRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
    REQUIRE(request_message->processing == message_20::datatypes::Processing::Ongoing);
}

SCENARIO("ISO15118-20 EV DC_WeldingDetection transitions to SessionStop on OK response") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::DC_WeldingDetection> primed{callbacks, no_seed};

    // enter() queues the only welding request, an Ongoing one; a Finished one is never emitted.
    {
        const auto enter_requests = primed.take_requests();
        const auto welding_request = enter_requests.get<message_20::DC_WeldingDetectionRequest>();
        REQUIRE(welding_request.has_value());
        REQUIRE(welding_request->processing == message_20::datatypes::Processing::Ongoing);
    }

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::SessionStop);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    // SessionStop::enter() queued a SessionStopRequest; no second welding request.
    const auto requests = primed.take_requests();
    REQUIRE_FALSE(requests.get<message_20::DC_WeldingDetectionRequest>().has_value());

    const auto session_stop_request = requests.get<message_20::SessionStopRequest>();
    REQUIRE(session_stop_request.has_value());
    REQUIRE(session_stop_request->charging_session == message_20::datatypes::ChargingSession::Terminate);
}

SCENARIO("ISO15118-20 EV DC_WeldingDetection stops session on FAILED_UnknownSession") {
    // State-specific rejection beyond the shared triple.
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::DC_WeldingDetection> primed{callbacks, no_seed};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::FAILED_UnknownSession));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_WeldingDetection);
    REQUIRE(primed.ctx.is_session_stopped() == true);
}

SCENARIO("ISO15118-20 EV DC_WeldingDetection rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](FsmStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        return fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<ev::d20::state::DC_WeldingDetection>()};
    };
    const auto make_ok = [](const message_20::Header& header) { return make_response(header, ResponseCode::OK); };
    const auto wrong = message_20::AuthorizationResponse{SESSION_HEADER, ResponseCode::OK,
                                                         message_20::datatypes::Processing::Finished};
    check_rejection_paths(callbacks, ev::d20::StateID::DC_WeldingDetection, make_fsm, make_ok, wrong);
}
