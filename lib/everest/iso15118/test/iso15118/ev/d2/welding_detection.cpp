// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d2/state/power_delivery.hpp>

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

const auto by_power_delivery = d2_entered_by([](D2StateHelper& helper) {
    return d2_successor_of<ev::d2::state::PowerDelivery>(
        helper, d2_ok<message_2::PowerDeliveryResponse>(),
        ev::d2::state::PowerDelivery::Stop{ev::session::StopReason::EvStop});
});

} // namespace

SCENARIO("ISO15118-2 EV WeldingDetection's request goes out on the PowerDelivery(Stop) transition") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::WeldingDetection> primed{callbacks, d2_no_seed, by_power_delivery};

    const auto request = primed.take_requests().get<message_2::WeldingDetectionRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->header.session_id == D2_SESSION_ID);
    REQUIRE(request->dc_ev_status.ev_ready == false);
}

SCENARIO("ISO15118-2 EV WeldingDetection moves to SessionStop once the voltage is safe") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::WeldingDetection> primed{callbacks, d2_no_seed, ev::session::StopReason::EvStop};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, 10.0));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::SessionStop);
    REQUIRE(primed.take_requests().get<message_2::SessionStopRequest>().has_value());
}

SCENARIO("ISO15118-2 EV WeldingDetection polls while the voltage is unsafe") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::WeldingDetection> primed{callbacks, ev::config::WeldingDetectionConfig{60.0f, 3},
                                                        d2_no_seed, by_power_delivery};

    for (int cycle = 0; cycle < 2; ++cycle) {
        REQUIRE(primed.helper.get_message_exchange().take_request().has_value());
        primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, 400.0));
        REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).output == ev::d2::Disposition::Awaiting);
    }

    // Exhausting the cycle bound fails detection and terminates the session.
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, 400.0));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::SessionStop);
}

SCENARIO("ISO15118-2 EV WeldingDetection rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](D2StateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(D2_SESSION_ID);
        return fsm::v2::FSM<ev::d2::StateBase>{
            ctx.create_state<ev::d2::state::WeldingDetection>(ev::session::StopReason::EvStop)};
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

SCENARIO("ISO15118-2 EV unsafe welding detection cannot preserve a pause") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::WeldingDetection> primed{callbacks, d2_no_seed, ev::session::StopReason::EvPause};
    for (int cycle = 0; cycle < 20 && primed.fsm.get_current_state_id() == ev::d2::StateID::WeldingDetection; ++cycle) {
        primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, 400.0));
        primed.feed(ev::d2::Event::V2GTP_MESSAGE);
    }
    const auto request = primed.take_requests().get<message_2::SessionStopRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->charging_session == dt::ChargingSession::Terminate);
}

SCENARIO("ISO15118-2 EV welding failure reports the configured cycle bound") {
    int failures = 0;
    ev::feedback::Callbacks callbacks{};
    callbacks.welding_detection_failed = [&](float voltage, int cycles) {
        ++failures;
        REQUIRE(voltage == 400.0f);
        REQUIRE(cycles == 2);
    };
    PrimedState<ev::d2::state::WeldingDetection> primed{callbacks, ev::config::WeldingDetectionConfig{60.0f, 2},
                                                        d2_no_seed, ev::session::StopReason::EvPause};
    for (int cycle = 0; cycle < 2; ++cycle) {
        primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, 400.0));
        primed.feed(ev::d2::Event::V2GTP_MESSAGE);
    }
    REQUIRE(failures == 1);
    const auto request = primed.take_requests().get<message_2::SessionStopRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->charging_session == dt::ChargingSession::Terminate);

    message_2::SessionStopResponse stop_response;
    stop_response.header = d2_header();
    stop_response.response_code = dt::ResponseCode::OK;
    primed.handle_response(stop_response);
    primed.feed(ev::d2::Event::V2GTP_MESSAGE);
    REQUIRE(primed.ctx.is_session_stopped());
    REQUIRE_FALSE(primed.ctx.is_session_paused());
    REQUIRE(failures == 1);
}

SCENARIO("ISO15118-2 EV welding waits for voltage strictly below the configured threshold") {
    int failures = 0;
    ev::feedback::Callbacks callbacks{};
    callbacks.welding_detection_failed = [&](float, int) { ++failures; };
    PrimedState<ev::d2::state::WeldingDetection> primed{callbacks, ev::config::WeldingDetectionConfig{42.0f, 3},
                                                        d2_no_seed, ev::session::StopReason::EvStop};
    for (const auto voltage : {400.0f, 42.0f, 41.0f}) {
        auto response = make_response(d2_header(), dt::ResponseCode::OK, voltage);

        primed.handle_response(response);
        primed.feed(ev::d2::Event::V2GTP_MESSAGE);
        if (voltage >= 42.0f) {
            const auto request = primed.take_requests().get<message_2::WeldingDetectionRequest>();
            REQUIRE(request.has_value());
            REQUIRE_FALSE(request->dc_ev_status.ev_ready);
        }
    }

    REQUIRE(primed.take_requests().get<message_2::SessionStopRequest>().has_value());
    REQUIRE(failures == 0);
}
