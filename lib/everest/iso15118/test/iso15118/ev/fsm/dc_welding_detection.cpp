// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/state/dc_welding_detection.hpp>
#include <iso15118/ev/d20/state/power_delivery.hpp>
#include <iso15118/ev/d20/state/session_stop.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/dc_welding_detection.hpp>
#include <iso15118/message/power_delivery.hpp>
#include <iso15118/message/session_stop.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

namespace {
using message_20::datatypes::ResponseCode;

message_20::DC_WeldingDetectionResponse make_response(const message_20::Header& header, ResponseCode code) {
    return message_20::DC_WeldingDetectionResponse{header, code, message_20::datatypes::RationalNumber{0, 0}};
}

ev::d20::SessionOptions with_welding(ev::config::WeldingDetectionConfig welding_detection) {
    ev::d20::SessionOptions options{};
    options.welding_detection = welding_detection;
    return options;
}

// Welding detection as an accepted DC PowerDelivery(Stop) leaves it: its first poll pending.
auto after_power_delivery_stop(ev::session::StopReason reason) {
    return entered_by([reason](FsmStateHelper& helper) {
        return successor_of<ev::d20::state::PowerDelivery>(
            helper, message_20::PowerDeliveryResponse{SESSION_HEADER, ResponseCode::OK, std::nullopt},
            ev::d20::state::PowerDelivery::Stop{reason});
    });
}
} // namespace

SCENARIO("ISO15118-20 EV DC_WeldingDetection is entered with an Ongoing request") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::DC_WeldingDetection> primed{callbacks, no_seed,
                                                            after_power_delivery_stop(ev::session::StopReason::EvStop)};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::DC_WeldingDetectionRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
    REQUIRE(request_message->processing == message_20::datatypes::Processing::Ongoing);
}

SCENARIO("ISO15118-20 EV DC_WeldingDetection closes with Finished before SessionStop") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::DC_WeldingDetection> primed{callbacks, no_seed,
                                                            after_power_delivery_stop(ev::session::StopReason::EvStop)};

    {
        const auto enter_requests = primed.take_requests();
        const auto welding_request = enter_requests.get<message_20::DC_WeldingDetectionRequest>();
        REQUIRE(welding_request.has_value());
        REQUIRE(welding_request->processing == message_20::datatypes::Processing::Ongoing);
    }

    // The answer to the Ongoing request buys the closing Finished one, not the transition:
    // the SECC only leaves welding detection once it has seen Finished.
    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK));
    const auto ongoing_result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(ongoing_result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_WeldingDetection);
    {
        const auto closing_requests = primed.take_requests();
        const auto welding_request = closing_requests.get<message_20::DC_WeldingDetectionRequest>();
        REQUIRE(welding_request.has_value());
        REQUIRE(welding_request->processing == message_20::datatypes::Processing::Finished);
        REQUIRE_FALSE(closing_requests.get<message_20::SessionStopRequest>().has_value());
    }

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::SessionStop);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    // The transition into SessionStop queued a SessionStopRequest; no third welding request.
    const auto requests = primed.take_requests();
    REQUIRE_FALSE(requests.get<message_20::DC_WeldingDetectionRequest>().has_value());

    const auto session_stop_request = requests.get<message_20::SessionStopRequest>();
    REQUIRE(session_stop_request.has_value());
    REQUIRE(session_stop_request->charging_session == message_20::datatypes::ChargingSession::Terminate);
}

SCENARIO("ISO15118-20 EV DC_WeldingDetection stops session on FAILED_UnknownSession") {
    // State-specific rejection beyond the shared triple.
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::DC_WeldingDetection> primed{callbacks, no_seed,
                                                            after_power_delivery_stop(ev::session::StopReason::EvStop)};

    expect_stops_session(primed, make_response(SESSION_HEADER, ResponseCode::FAILED_UnknownSession),
                         ev::d20::StateID::DC_WeldingDetection);
}

SCENARIO("ISO15118-20 EV DC_WeldingDetection rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](FsmStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        return fsm::v2::FSM<ev::d20::StateBase>{
            ctx.create_state<ev::d20::state::DC_WeldingDetection>(ev::session::StopReason::EvStop)};
    };
    const auto make_ok = [](const message_20::Header& header) { return make_response(header, ResponseCode::OK); };
    const auto wrong = message_20::AuthorizationResponse{SESSION_HEADER, ResponseCode::OK,
                                                         message_20::datatypes::Processing::Finished};
    check_rejection_paths(callbacks, ev::d20::StateID::DC_WeldingDetection, make_fsm, make_ok, wrong);
}

SCENARIO("ISO15118-20 EV welding detection keeps polling unsafe voltage") {
    ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::DC_WeldingDetection> primed{callbacks, no_seed,
                                                            after_power_delivery_stop(ev::session::StopReason::EvStop)};
    auto response = make_response(SESSION_HEADER, ResponseCode::OK);
    response.present_voltage = message_20::datatypes::from_float(400.0f);
    primed.handle_response(response);
    primed.feed(ev::d20::Event::V2GTP_MESSAGE);
    const auto request = primed.take_requests().get<message_20::DC_WeldingDetectionRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->processing == message_20::datatypes::Processing::Ongoing);
}

SCENARIO("ISO15118-20 EV welding failure reports unsafe voltage and cancels pause") {
    int failures = 0;
    ev::feedback::Callbacks callbacks{};
    callbacks.welding_detection_failed = [&](float voltage, int cycles) {
        ++failures;
        REQUIRE(voltage == 400.0f);
        REQUIRE(cycles == 2);
    };
    ev::config::WeldingDetectionConfig welding_detection{};
    welding_detection.max_cycles = 2;
    PrimedState<ev::d20::state::DC_WeldingDetection> primed{
        callbacks, message_20::datatypes::ServiceCategory::DC, with_welding(welding_detection), no_seed,
        after_power_delivery_stop(ev::session::StopReason::EvPause)};
    for (int cycle = 0; cycle < 2 && primed.fsm.get_current_state_id() == ev::d20::StateID::DC_WeldingDetection;
         ++cycle) {
        auto response = make_response(SESSION_HEADER, ResponseCode::OK);
        response.present_voltage = message_20::datatypes::from_float(400.0f);
        primed.handle_response(response);
        primed.feed(ev::d20::Event::V2GTP_MESSAGE);
    }
    REQUIRE(failures == 1);
    const auto closing = primed.take_requests();
    REQUIRE(closing.get<message_20::DC_WeldingDetectionRequest>().has_value());
    REQUIRE(closing.get<message_20::DC_WeldingDetectionRequest>()->processing ==
            message_20::datatypes::Processing::Finished);
    REQUIRE_FALSE(closing.get<message_20::SessionStopRequest>().has_value());
    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK));
    primed.feed(ev::d20::Event::V2GTP_MESSAGE);
    const auto request = primed.take_requests().get<message_20::SessionStopRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->charging_session == message_20::datatypes::ChargingSession::Terminate);

    message_20::SessionStopResponse stop_response;
    stop_response.header = SESSION_HEADER;
    stop_response.response_code = ResponseCode::OK;
    primed.handle_response(stop_response);
    primed.feed(ev::d20::Event::V2GTP_MESSAGE);
    REQUIRE(primed.ctx.is_session_stopped());
    REQUIRE_FALSE(primed.ctx.is_session_paused());
    REQUIRE(failures == 1);
}

SCENARIO("ISO15118-20 EV welding waits for voltage strictly below the configured threshold") {
    int failures = 0;
    ev::feedback::Callbacks callbacks{};
    callbacks.welding_detection_failed = [&](float, int) { ++failures; };
    PrimedState<ev::d20::state::DC_WeldingDetection> primed{callbacks, message_20::datatypes::ServiceCategory::DC,
                                                            with_welding({42.0f, 3, std::chrono::seconds(10)}), no_seed,
                                                            after_power_delivery_stop(ev::session::StopReason::EvStop)};
    for (const auto voltage : {400.0f, 42.0f, 41.0f}) {
        auto response = make_response(SESSION_HEADER, ResponseCode::OK);
        response.present_voltage = message_20::datatypes::from_float(voltage);
        primed.handle_response(response);
        primed.feed(ev::d20::Event::V2GTP_MESSAGE);
        if (voltage >= 42.0f) {
            const auto request = primed.take_requests().get<message_20::DC_WeldingDetectionRequest>();
            REQUIRE(request.has_value());
            REQUIRE(request->processing == message_20::datatypes::Processing::Ongoing);
        }
    }

    const auto closing = primed.take_requests().get<message_20::DC_WeldingDetectionRequest>();
    REQUIRE(closing.has_value());
    REQUIRE(closing->processing == message_20::datatypes::Processing::Finished);
    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK));
    primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(primed.take_requests().get<message_20::SessionStopRequest>().has_value());
    REQUIRE(failures == 0);
}

SCENARIO("ISO15118-20 EV safe welding ignores deadline while awaiting Finished response") {
    int failures = 0;
    ev::feedback::Callbacks callbacks{};
    callbacks.welding_detection_failed = [&](float, int) { ++failures; };
    PrimedState<ev::d20::state::DC_WeldingDetection> primed{
        callbacks, no_seed, after_power_delivery_stop(ev::session::StopReason::EvPause)};
    primed.take_requests();
    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK));
    primed.feed(ev::d20::Event::V2GTP_MESSAGE);
    SECTION("Finished is on the wire") {
        REQUIRE(primed.take_requests().get<message_20::DC_WeldingDetectionRequest>()->processing ==
                message_20::datatypes::Processing::Finished);
        primed.feed(ev::d20::Event::WELDING_TIMEOUT);
    }
    SECTION("Finished is waiting to transmit") {
        primed.feed(ev::d20::Event::WELDING_TIMEOUT);
        REQUIRE(primed.take_requests().get<message_20::DC_WeldingDetectionRequest>()->processing ==
                message_20::datatypes::Processing::Finished);
    }
    REQUIRE(failures == 0);
    REQUIRE_FALSE(primed.take_requests().get<message_20::SessionStopRequest>().has_value());
    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK));
    primed.feed(ev::d20::Event::V2GTP_MESSAGE);
    REQUIRE(primed.take_requests().get<message_20::SessionStopRequest>()->charging_session ==
            message_20::datatypes::ChargingSession::Pause);
    REQUIRE(failures == 0);
}

SCENARIO("ISO15118-20 EV welding deadline closes with Finished before termination") {
    int failures = 0;
    ev::feedback::Callbacks callbacks{};
    callbacks.welding_detection_failed = [&](float, int) { ++failures; };
    // The deadline is the check's own timeout, so an answer arriving after it is already past the bound.
    PrimedState<ev::d20::state::DC_WeldingDetection> primed{
        callbacks, message_20::datatypes::ServiceCategory::DC, with_welding({60.0f, 10, std::chrono::seconds(0)}),
        no_seed, after_power_delivery_stop(ev::session::StopReason::EvPause)};
    SECTION("between cycles") {
        primed.feed(ev::d20::Event::WELDING_TIMEOUT);
    }
    SECTION("with a request in flight") {
        primed.take_requests();
        primed.feed(ev::d20::Event::WELDING_TIMEOUT);
        REQUIRE_FALSE(primed.take_requests().get<message_20::DC_WeldingDetectionRequest>().has_value());
        REQUIRE_FALSE(primed.ctx.is_session_stopped());
        REQUIRE(failures == 0);
        auto response = make_response(SESSION_HEADER, ResponseCode::OK);
        response.present_voltage = message_20::datatypes::from_float(400.0f);
        primed.handle_response(response);
        primed.feed(ev::d20::Event::V2GTP_MESSAGE);
    }
    REQUIRE(failures == 1);
    const auto closing = primed.take_requests();
    REQUIRE(closing.get<message_20::DC_WeldingDetectionRequest>().has_value());
    REQUIRE(closing.get<message_20::DC_WeldingDetectionRequest>()->processing ==
            message_20::datatypes::Processing::Finished);
    REQUIRE_FALSE(closing.get<message_20::SessionStopRequest>().has_value());
    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK));
    primed.feed(ev::d20::Event::V2GTP_MESSAGE);
    REQUIRE(primed.take_requests().get<message_20::SessionStopRequest>()->charging_session ==
            message_20::datatypes::ChargingSession::Terminate);
    REQUIRE(failures == 1);
}

SCENARIO("ISO15118-20 EV safe welding response after deadline preserves pause") {
    int failures = 0;
    ev::feedback::Callbacks callbacks{};
    callbacks.welding_detection_failed = [&](float, int) { ++failures; };
    PrimedState<ev::d20::state::DC_WeldingDetection> primed{
        callbacks, no_seed, after_power_delivery_stop(ev::session::StopReason::EvPause)};
    primed.take_requests();
    primed.feed(ev::d20::Event::WELDING_TIMEOUT);
    REQUIRE_FALSE(primed.take_requests().get<message_20::DC_WeldingDetectionRequest>().has_value());
    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK));
    primed.feed(ev::d20::Event::V2GTP_MESSAGE);
    REQUIRE(failures == 0);
    const auto closing = primed.take_requests();
    REQUIRE(closing.get<message_20::DC_WeldingDetectionRequest>().has_value());
    REQUIRE(closing.get<message_20::DC_WeldingDetectionRequest>()->processing ==
            message_20::datatypes::Processing::Finished);
    REQUIRE_FALSE(closing.get<message_20::SessionStopRequest>().has_value());
    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK));
    primed.feed(ev::d20::Event::V2GTP_MESSAGE);
    REQUIRE(primed.take_requests().get<message_20::SessionStopRequest>()->charging_session ==
            message_20::datatypes::ChargingSession::Pause);
    REQUIRE(failures == 0);
}

SCENARIO("ISO15118-20 EV missing Finished response preserves the welding outcome") {
    int failures = 0;
    ev::feedback::Callbacks callbacks{};
    callbacks.welding_detection_failed = [&](float, int) { ++failures; };
    PrimedState<ev::d20::state::DC_WeldingDetection> primed{
        callbacks, message_20::datatypes::ServiceCategory::DC, with_welding({60.0f, 1, std::chrono::seconds(10)}),
        no_seed, after_power_delivery_stop(ev::session::StopReason::EvPause)};
    primed.take_requests();
    auto response = make_response(SESSION_HEADER, ResponseCode::OK);
    int expected_failures = 0;
    SECTION("safe check") {
    }
    SECTION("failed check") {
        response.present_voltage = message_20::datatypes::from_float(400.0f);
        expected_failures = 1;
    }
    primed.handle_response(response);
    primed.feed(ev::d20::Event::V2GTP_MESSAGE);
    REQUIRE(failures == expected_failures);
    const auto closing = primed.take_requests();
    REQUIRE(closing.get<message_20::DC_WeldingDetectionRequest>().has_value());
    REQUIRE(closing.get<message_20::DC_WeldingDetectionRequest>()->processing ==
            message_20::datatypes::Processing::Finished);
    primed.feed(ev::d20::Event::RESPONSE_TIMEOUT);
    REQUIRE(primed.ctx.is_session_stopped());
    REQUIRE_FALSE(primed.take_requests().get<message_20::SessionStopRequest>().has_value());
    REQUIRE(failures == expected_failures);
}
