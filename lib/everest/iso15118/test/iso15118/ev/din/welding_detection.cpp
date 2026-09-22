// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/din/state/power_delivery.hpp>
#include <iso15118/ev/din/state/welding_detection.hpp>
#include <iso15118/message_din/power_delivery.hpp>
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
const auto by_power_delivery_stop = entered_by([](DinStateHelper& helper) {
    message_din::PowerDeliveryResponse res{};
    res.header = header();
    res.response_code = dt::ResponseCode::OK;
    using ev::din::state::PowerDelivery;
    return successor_of<PowerDelivery>(helper, res, PowerDelivery::Stop{ev::session::StopReason::EvStop});
});

} // namespace

SCENARIO("DIN 70121 EV WeldingDetection's request with EVReady cleared goes out on PowerDelivery(Stop)") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed, by_power_delivery_stop};

    const auto request = primed.take_requests().get<message_din::WeldingDetectionRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->dc_ev_status.ev_ready == false);
}

SCENARIO("DIN 70121 EV WeldingDetection transitions to SessionStop on a safe voltage") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, no_seed, ev::session::StopReason::EvStop};

    primed.handle_response(make_response(header(), dt::ResponseCode::OK, 20.0));
    const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == StateID::SessionStop);
    REQUIRE(primed.take_requests().get<message_din::SessionStopRequest>().has_value());
}

SCENARIO("DIN 70121 EV WeldingDetection fails detection at the configured cycle bound") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, ev::config::WeldingDetectionConfig{60.0f, 3}, no_seed,
                              ev::session::StopReason::EvStop};

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
        return fsm::v2::FSM<ev::din::StateBase>{ctx.create_state<State>(ev::session::StopReason::EvStop)};
    };
    message_din::SessionStopResponse wrong;
    wrong.header = header();
    wrong.response_code = dt::ResponseCode::OK;
    check_rejection_paths(callbacks, StateID::WeldingDetection, make_fsm, ok_response, wrong);
}

SCENARIO("DIN 70121 EV welding failure reports unsafe voltage and cancels pause") {
    int failures = 0;
    ev::feedback::Callbacks callbacks{};
    callbacks.welding_detection_failed = [&](float voltage, int cycles) {
        ++failures;
        REQUIRE(voltage == 400.0f);
        REQUIRE(cycles == 2);
    };
    PrimedState<ev::din::state::WeldingDetection> primed{callbacks, ev::config::WeldingDetectionConfig{60.0f, 2},
                                                         no_seed, ev::session::StopReason::EvPause};
    for (int cycle = 0; cycle < 2 && primed.fsm.get_current_state_id() == ev::din::StateID::WeldingDetection; ++cycle) {
        auto response = make_response(header(), dt::ResponseCode::OK, 400.0);
        primed.handle_response(response);
        primed.feed(ev::din::Event::V2GTP_MESSAGE);
    }
    REQUIRE(failures == 1);
    const auto request = primed.take_requests().get<message_din::SessionStopRequest>();
    REQUIRE(request.has_value());

    message_din::SessionStopResponse stop_response;
    stop_response.header = header();
    stop_response.response_code = dt::ResponseCode::OK;
    primed.handle_response(stop_response);
    primed.feed(ev::din::Event::V2GTP_MESSAGE);
    REQUIRE(primed.ctx.is_session_stopped());
    REQUIRE_FALSE(primed.ctx.is_session_paused());
    REQUIRE(failures == 1);
}

SCENARIO("DIN 70121 EV welding waits for voltage strictly below the configured threshold") {
    int failures = 0;
    ev::feedback::Callbacks callbacks{};
    callbacks.welding_detection_failed = [&](float, int) { ++failures; };
    PrimedState<ev::din::state::WeldingDetection> primed{callbacks, ev::config::WeldingDetectionConfig{42.0f, 3},
                                                         no_seed, ev::session::StopReason::EvStop};
    for (const auto voltage : {400.0f, 42.0f, 41.0f}) {
        auto response = make_response(header(), dt::ResponseCode::OK, voltage);

        primed.handle_response(response);
        primed.feed(ev::din::Event::V2GTP_MESSAGE);
        if (voltage >= 42.0f) {
            const auto request = primed.take_requests().get<message_din::WeldingDetectionRequest>();
            REQUIRE(request.has_value());
            REQUIRE_FALSE(request->dc_ev_status.ev_ready);
        }
    }

    REQUIRE(primed.take_requests().get<message_din::SessionStopRequest>().has_value());
    REQUIRE(failures == 0);
}
