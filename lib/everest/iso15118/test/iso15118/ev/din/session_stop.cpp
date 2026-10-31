// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "helper.hpp"

#include <iso15118/ev/detail/din/context_helper.hpp>
#include <iso15118/ev/din/state/session_stop.hpp>
#include <iso15118/ev/din/state/welding_detection.hpp>
#include <iso15118/io/logging.hpp>
#include <iso15118/message_din/session_stop.hpp>
#include <iso15118/message_din/welding_detection.hpp>

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
const auto by_welding_detection = entered_by([](DinStateHelper& helper) {
    message_din::WeldingDetectionResponse res{};
    res.header = header();
    res.response_code = dt::ResponseCode::OK;
    return successor_of<ev::din::state::WeldingDetection>(helper, res, ev::session::StopReason::EvStop);
});

} // namespace

SCENARIO("DIN 70121 EV SessionStop's request goes out on the WeldingDetection transition") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed, by_welding_detection};

    const auto request = primed.take_requests().get<message_din::SessionStopRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->header.session_id == SESSION_ID);
}

SCENARIO("DIN 70121 EV SessionStop terminates the session on either side's stop") {
    const auto reason = GENERATE(ev::session::StopReason::EvStop, ev::session::StopReason::EvseTerminate);
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed, reason};

    // A pause latched after leaving the loop does not turn the stop into one.
    primed.ctx.set_pause_charging_requested(true);
    primed.handle_response(ok_response(header()));
    const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(result.output == ev::din::Disposition::Stopping);
    REQUIRE(primed.ctx.is_session_stopped() == true);
    REQUIRE(primed.ctx.is_session_paused() == false);
}

SCENARIO("DIN 70121 EV SessionStop pauses the session on a pause request") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed, ev::session::StopReason::EvPause};

    primed.handle_response(ok_response(header()));
    primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(primed.ctx.is_session_stopped() == true);
    // The paused session id is re-joined by the next session.
    REQUIRE(primed.ctx.is_session_paused() == true);
}

SCENARIO("DIN 70121 EV reports an undecodable response once") {
    std::vector<std::string> errors;
    io::set_logging_callback([&errors](LogLevel level, std::string message) {
        if (level == LogLevel::Error) {
            errors.push_back(std::move(message));
        }
    });
    const ev::feedback::Callbacks callbacks{};
    DinStateHelper helper{callbacks};
    auto& ctx = helper.get_context();
    ctx.set_session_id(SESSION_ID);

    const std::array<uint8_t, 4> bytes{0xff, 0xff, 0xff, 0xff};
    const message_din::ResponseVariant variant{io::StreamInputView{bytes.data(), bytes.size()}};
    const auto* res = ev::din::expect_response<message_din::SessionStopResponse>(ctx, variant);
    io::set_logging_callback([](LogLevel, const std::string&) {});

    REQUIRE(res == nullptr);
    REQUIRE(ctx.is_session_stopped());
    REQUIRE(errors.size() == 1);
    REQUIRE(errors.front().find(variant.get_error()) != std::string::npos);
}
