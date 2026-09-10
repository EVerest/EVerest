// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/din/state/session_stop.hpp>
#include <iso15118/message_din/session_stop.hpp>

using namespace iso15118;

namespace {
namespace dt = message_din::datatypes;
using ev::din::StateID;
using State = ev::din::state::SessionStop;

message_din::SessionStopResponse ok_response(const message_din::Header& hdr) {
    message_din::SessionStopResponse res;
    res.header = hdr;
    res.response_code = dt::ResponseCode::OK;
    return res;
}
} // namespace

SCENARIO("DIN 70121 EV SessionStop sends the request on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed};

    const auto request = primed.take_requests().get<message_din::SessionStopRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->header.session_id == SESSION_ID);
}

SCENARIO("DIN 70121 EV SessionStop terminates the session on a stop request") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed};

    primed.ctx.set_stop_charging_requested(true);
    primed.handle_response(ok_response(header()));
    const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(result.output == ev::din::Disposition::Stopping);
    REQUIRE(primed.ctx.is_session_stopped() == true);
    REQUIRE(primed.ctx.is_session_paused() == false);
}

SCENARIO("DIN 70121 EV SessionStop pauses the session on a pause request") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed};

    primed.ctx.set_pause_charging_requested(true);
    primed.handle_response(ok_response(header()));
    primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(primed.ctx.is_session_stopped() == true);
    // The paused session id is re-joined by the next session.
    REQUIRE(primed.ctx.is_session_paused() == true);
}
