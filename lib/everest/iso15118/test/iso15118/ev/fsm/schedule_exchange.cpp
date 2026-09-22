// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <stdexcept>

#include "helper.hpp"

#include <iso15118/ev/d20/state/dc_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/schedule_exchange.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/dc_cable_check.hpp>
#include <iso15118/message/dc_charge_parameter_discovery.hpp>
#include <iso15118/message/dc_pre_charge.hpp>
#include <iso15118/message/power_delivery.hpp>
#include <iso15118/message/schedule_exchange.hpp>
#include <iso15118/message/session_stop.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

namespace {
using message_20::datatypes::Processing;
using message_20::datatypes::ResponseCode;
using message_20::datatypes::ServiceCategory;

message_20::ScheduleExchangeResponse make_response(const message_20::Header& header, ResponseCode response_code,
                                                   Processing processing) {
    message_20::ScheduleExchangeResponse res{};
    res.header = header;
    res.response_code = response_code;
    res.processing = processing;
    return res;
}

// ScheduleExchange's first request, sent by DC_ChargeParameterDiscovery on its OK.
template <typename Seed> DecodedRequests first_request(Seed seed) {
    auto discovery = Predecessor::dc<ev::d20::state::DC_ChargeParameterDiscovery>();
    seed(*discovery.helper);
    return discovery.transitions_to(
        ev::d20::StateID::ScheduleExchange,
        message_20::DC_ChargeParameterDiscoveryResponse{SESSION_HEADER, ResponseCode::OK,
                                                        message_20::datatypes::DC_CPDResEnergyTransferMode{}});
}
} // namespace

SCENARIO("ISO15118-20 EV ChargeParameterDiscovery sends the initial Dynamic ScheduleExchangeRequest") {
    const auto requests = first_request(no_seed);
    const auto request_message = requests.get<message_20::ScheduleExchangeRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
    REQUIRE(std::holds_alternative<message_20::datatypes::Dynamic_SEReqControlMode>(request_message->control_mode));
}

SCENARIO("ISO15118-20 EV ScheduleExchange fires ev_power_ready and transitions to DC_CableCheck on Finished") {
    bool ev_power_ready_fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.ev_power_ready = [&ev_power_ready_fired]() { ev_power_ready_fired = true; };
    PrimedState<ev::d20::state::ScheduleExchange> primed{callbacks, no_seed};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, Processing::Finished));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(ev_power_ready_fired == true);
    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_CableCheck);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO(
    "ISO15118-20 EV ScheduleExchange fires ev_power_ready and transitions to DC_CableCheck on Finished for DC_BPT") {
    bool ev_power_ready_fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.ev_power_ready = [&ev_power_ready_fired]() { ev_power_ready_fired = true; };
    PrimedState<ev::d20::state::ScheduleExchange> primed{callbacks, message_20::datatypes::ServiceCategory::DC_BPT,
                                                         no_seed};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, Processing::Finished));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(ev_power_ready_fired == true);
    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::DC_CableCheck);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV ScheduleExchange fires ev_power_ready and transitions to PowerDelivery on Finished for AC") {
    bool ev_power_ready_fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.ev_power_ready = [&ev_power_ready_fired]() { ev_power_ready_fired = true; };
    PrimedState<ev::d20::state::ScheduleExchange> primed{callbacks, ServiceCategory::AC, no_seed};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, Processing::Finished));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(ev_power_ready_fired == true);
    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO(
    "ISO15118-20 EV ScheduleExchange fires ev_power_ready and transitions to PowerDelivery on Finished for AC_BPT") {
    bool ev_power_ready_fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.ev_power_ready = [&ev_power_ready_fired]() { ev_power_ready_fired = true; };
    PrimedState<ev::d20::state::ScheduleExchange> primed{callbacks, message_20::datatypes::ServiceCategory::AC_BPT,
                                                         no_seed};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, Processing::Finished));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(ev_power_ready_fired == true);
    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("ISO15118-20 EV ScheduleExchange transitions to PowerDelivery on Finished for AC_DER_SAE") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ScheduleExchange> primed{callbacks, ServiceCategory::AC_DER_SAE, no_seed,
                                                         ev::d20::der::SaeNegotiation{}};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, Processing::Finished));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::PowerDelivery);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::PowerDeliveryRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->charge_progress == message_20::datatypes::Progress::Start);
}

SCENARIO("ISO15118-20 EV ScheduleExchange goes to SessionStop when a stop was requested") {
    // [V2G20-2644]: a stopped session never signals power readiness; SessionStopReq(Terminate)
    // is the next request instead of DC_CableCheckReq.
    bool ev_power_ready_fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.ev_power_ready = [&ev_power_ready_fired]() { ev_power_ready_fired = true; };
    PrimedState<ev::d20::state::ScheduleExchange> primed{callbacks, no_seed};
    primed.ctx.set_stop_charging_requested(true);

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, Processing::Finished));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::SessionStop);
    REQUIRE(ev_power_ready_fired == false);

    const auto requests = primed.take_requests();
    const auto stop_request = requests.get<message_20::SessionStopRequest>();
    REQUIRE(stop_request.has_value());
    REQUIRE(stop_request->charging_session == message_20::datatypes::ChargingSession::Terminate);
}

SCENARIO("ISO15118-20 EV ScheduleExchange stays and resends on Ongoing without firing ev_power_ready") {
    bool ev_power_ready_fired = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.ev_power_ready = [&ev_power_ready_fired]() { ev_power_ready_fired = true; };
    PrimedState<ev::d20::state::ScheduleExchange> primed{callbacks, no_seed};

    // The first request went out on the transition into the state, so anything pending after
    // the feed is a fresh ScheduleExchangeRequest.
    REQUIRE_FALSE(primed.helper.get_message_exchange().has_request());

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, Processing::Ongoing));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(ev_power_ready_fired == false);
    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::ScheduleExchange);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::ScheduleExchangeRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);
}

SCENARIO("ISO15118-20 EV ScheduleExchange for AC_DER_SAE needs the negotiated SAE settings") {
    const ev::feedback::Callbacks callbacks{};
    FsmStateHelper helper{callbacks, DEFAULT_APP_PROTOCOLS, ServiceCategory::AC_DER_SAE};

    REQUIRE_THROWS_AS(helper.get_context().create_state<ev::d20::state::ScheduleExchange>(), std::logic_error);
}

SCENARIO("ISO15118-20 EV ScheduleExchange rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](FsmStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        return fsm::v2::FSM<ev::d20::StateBase>{ctx.create_state<ev::d20::state::ScheduleExchange>()};
    };
    const auto make_ok = [](const message_20::Header& header) {
        return make_response(header, ResponseCode::OK, Processing::Finished);
    };
    const auto wrong = message_20::AuthorizationResponse{SESSION_HEADER, ResponseCode::OK, Processing::Finished};
    check_rejection_paths(callbacks, ev::d20::StateID::ScheduleExchange, make_fsm, make_ok, wrong);
}

namespace {
using message_20::datatypes::ControlMode;

// ServiceDetail records the selected control mode before ScheduleExchange is entered.
const auto seed_scheduled = [](FsmStateHelper& helper) {
    helper.get_context().set_selected_control_mode(ControlMode::Scheduled);
};

message_20::ScheduleExchangeResponse make_scheduled_response(const message_20::Header& header,
                                                             ResponseCode response_code, Processing processing,
                                                             std::vector<uint32_t> tuple_ids) {
    auto res = make_response(header, response_code, processing);
    message_20::datatypes::Scheduled_SEResControlMode mode{};
    for (const auto id : tuple_ids) {
        message_20::datatypes::ScheduleTuple tuple{};
        tuple.schedule_tuple_id = id;
        mode.schedule_tuple.push_back(tuple);
    }
    res.control_mode = mode;
    return res;
}
} // namespace

SCENARIO("ISO15118-20 EV ChargeParameterDiscovery sends a Scheduled ScheduleExchangeRequest in Scheduled mode") {
    const auto requests = first_request(seed_scheduled);
    const auto request_message = requests.get<message_20::ScheduleExchangeRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(std::holds_alternative<message_20::datatypes::Scheduled_SEReqControlMode>(request_message->control_mode));
    const auto& mode = std::get<message_20::datatypes::Scheduled_SEReqControlMode>(request_message->control_mode);
    REQUIRE_FALSE(mode.departure_time.has_value());
    REQUIRE_FALSE(mode.target_energy.has_value());
    REQUIRE_FALSE(mode.energy_offer.has_value());
}

namespace {
// The ScheduleTupleID a PowerDeliveryReq(Start) names in its Scheduled power profile.
std::optional<uint8_t> named_schedule_tuple(const DecodedRequests& requests) {
    const auto request = requests.get<message_20::PowerDeliveryRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->charge_progress == message_20::datatypes::Progress::Start);
    if (not request->power_profile.has_value()) {
        return std::nullopt;
    }
    const auto* mode =
        std::get_if<message_20::datatypes::Scheduled_EVPPTControlMode>(&request->power_profile->control_mode);
    REQUIRE(mode != nullptr);
    return mode->selected_schedule;
}

template <typename Primed> void feed_expecting(Primed& primed, ev::d20::Event event, ev::d20::StateID expected) {
    primed.feed(event);
    REQUIRE(primed.fsm.get_current_state_id() == expected);
    REQUIRE_FALSE(primed.ctx.is_session_stopped());
}
} // namespace

SCENARIO("ISO15118-20 EV ScheduleExchange hands the offered ScheduleTupleID to PowerDelivery on Finished for AC") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ScheduleExchange> primed{callbacks, ServiceCategory::AC, seed_scheduled};

    primed.handle_response(make_scheduled_response(SESSION_HEADER, ResponseCode::OK, Processing::Finished, {4, 9}));
    feed_expecting(primed, ev::d20::Event::V2GTP_MESSAGE, ev::d20::StateID::PowerDelivery);

    REQUIRE(named_schedule_tuple(primed.take_requests()) == 4);
}

SCENARIO("ISO15118-20 EV ScheduleExchange's ScheduleTupleID reaches PowerDelivery through the DC precharge") {
    const ev::feedback::Callbacks callbacks{};
    ev::d20::SessionOptions options{};
    options.has_cp_state_feedback = true;
    const auto seed = [](FsmStateHelper& helper) {
        seed_scheduled(helper);
        ev::params::DcChargeParams params{};
        params.target_voltage = 400.0f;
        params.max_charge_power = 11000.0f;
        helper.set_dc_params(params);
    };
    PrimedState<ev::d20::state::ScheduleExchange> primed{callbacks, ServiceCategory::DC, options, seed};
    using ev::d20::Event;
    using ev::d20::StateID;
    using message_20::datatypes::from_float;

    // Every retry and wait on the way keeps the tuple.
    primed.handle_response(make_scheduled_response(SESSION_HEADER, ResponseCode::OK, Processing::Ongoing, {}));
    feed_expecting(primed, Event::V2GTP_MESSAGE, StateID::ScheduleExchange);
    primed.handle_response(make_scheduled_response(SESSION_HEADER, ResponseCode::OK, Processing::Finished, {4, 9}));
    feed_expecting(primed, Event::V2GTP_MESSAGE, StateID::DC_CableCheck);
    // [V2G20-1546]: the Context holds the pick from here on, for Start, Stop and Standby.
    REQUIRE(primed.ctx.schedule_tuple_id() == 4);

    primed.ctx.set_cp_state(true);
    feed_expecting(primed, Event::CONTROL_MESSAGE, StateID::DC_CableCheck);
    primed.handle_response(message_20::DC_CableCheckResponse{SESSION_HEADER, ResponseCode::OK, Processing::Ongoing});
    feed_expecting(primed, Event::V2GTP_MESSAGE, StateID::DC_CableCheck);
    primed.handle_response(message_20::DC_CableCheckResponse{SESSION_HEADER, ResponseCode::OK, Processing::Finished});
    feed_expecting(primed, Event::V2GTP_MESSAGE, StateID::DC_PreCharge);

    primed.handle_response(message_20::DC_PreChargeResponse{SESSION_HEADER, ResponseCode::OK, from_float(100.0f)});
    feed_expecting(primed, Event::V2GTP_MESSAGE, StateID::DC_PreCharge);
    primed.handle_response(message_20::DC_PreChargeResponse{SESSION_HEADER, ResponseCode::OK, from_float(400.0f)});
    feed_expecting(primed, Event::V2GTP_MESSAGE, StateID::DC_PreCharge);
    primed.handle_response(message_20::DC_PreChargeResponse{SESSION_HEADER, ResponseCode::OK, from_float(400.0f)});
    feed_expecting(primed, Event::V2GTP_MESSAGE, StateID::PowerDelivery);

    REQUIRE(named_schedule_tuple(primed.take_requests()) == 4);
}

SCENARIO("ISO15118-20 EV ScheduleExchange re-sends the Scheduled request on Ongoing") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ScheduleExchange> primed{callbacks, seed_scheduled};

    REQUIRE_FALSE(primed.helper.get_message_exchange().has_request());

    primed.handle_response(make_scheduled_response(SESSION_HEADER, ResponseCode::OK, Processing::Ongoing, {}));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == false);
    REQUIRE(primed.ctx.is_session_stopped() == false);

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::ScheduleExchangeRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(std::holds_alternative<message_20::datatypes::Scheduled_SEReqControlMode>(request_message->control_mode));
}

SCENARIO("ISO15118-20 EV ScheduleExchange stops the session on a Dynamic reply it never requested") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ScheduleExchange> primed{callbacks, seed_scheduled};

    expect_stops_session(primed, make_response(SESSION_HEADER, ResponseCode::OK, Processing::Finished),
                         ev::d20::StateID::ScheduleExchange);
}

SCENARIO("ISO15118-20 EV ScheduleExchange stops the session on a Scheduled reply without a schedule tuple") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ScheduleExchange> primed{callbacks, seed_scheduled};

    expect_stops_session(primed, make_scheduled_response(SESSION_HEADER, ResponseCode::OK, Processing::Finished, {}),
                         ev::d20::StateID::ScheduleExchange);
}

SCENARIO("ISO15118-20 EV ScheduleExchange stops the session on a ScheduleTupleID outside 1..255") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ScheduleExchange> primed{callbacks, seed_scheduled};

    expect_stops_session(primed, make_scheduled_response(SESSION_HEADER, ResponseCode::OK, Processing::Finished, {256}),
                         ev::d20::StateID::ScheduleExchange);
}

SCENARIO("ISO15118-20 EV ScheduleExchange goes to SessionStop carrying Terminate when a pause was requested") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::ScheduleExchange> primed{callbacks, no_seed};
    primed.ctx.set_pause_charging_requested(true);

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, Processing::Finished));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::SessionStop);

    const auto requests = primed.take_requests();
    const auto stop_request = requests.get<message_20::SessionStopRequest>();
    REQUIRE(stop_request.has_value());
    // [V2G20-2644]: before PowerDeliveryReq(Start) a pause terminates too.
    REQUIRE(stop_request->charging_session == message_20::datatypes::ChargingSession::Terminate);
}

namespace {
using namespace std::chrono_literals;

// ScheduleExchange's first request in a session start_charging configured with `options`.
template <typename Seed> DecodedRequests first_request(ev::d20::SessionOptions options, Seed seed) {
    auto discovery = Predecessor::in<ev::d20::state::DC_ChargeParameterDiscovery>(ServiceCategory::DC, options);
    use_hand_steady_clock(*discovery.helper);
    ev::params::DcChargeParams params{};
    params.energy_capacity = 80000.0f;
    discovery.helper->set_dc_params(params);
    seed(*discovery.helper);
    return discovery.transitions_to(
        ev::d20::StateID::ScheduleExchange,
        message_20::DC_ChargeParameterDiscoveryResponse{SESSION_HEADER, ResponseCode::OK,
                                                        message_20::datatypes::DC_CPDResEnergyTransferMode{}});
}

message_20::datatypes::Dynamic_SEReqControlMode dynamic_mode(const DecodedRequests& requests) {
    const auto request = requests.get<message_20::ScheduleExchangeRequest>();
    REQUIRE(request.has_value());
    REQUIRE(std::holds_alternative<message_20::datatypes::Dynamic_SEReqControlMode>(request->control_mode));
    return std::get<message_20::datatypes::Dynamic_SEReqControlMode>(request->control_mode);
}

message_20::datatypes::Scheduled_SEReqControlMode scheduled_mode(const DecodedRequests& requests) {
    const auto request = requests.get<message_20::ScheduleExchangeRequest>();
    REQUIRE(request.has_value());
    REQUIRE(std::holds_alternative<message_20::datatypes::Scheduled_SEReqControlMode>(request->control_mode));
    return std::get<message_20::datatypes::Scheduled_SEReqControlMode>(request->control_mode);
}
} // namespace

SCENARIO("ISO15118-20 EV Dynamic ScheduleExchangeRequest states start_charging's energy and departure time") {
    const auto mode = dynamic_mode(first_request(with_energy_request(20000.0f, 3600s), no_seed));
    REQUIRE(mode.departure_time == 3600U);
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.target_energy) == Catch::Approx(20000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.max_energy) == Catch::Approx(20000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.min_energy) == Catch::Approx(0.0f));
}

SCENARIO("ISO15118-20 EV Dynamic ScheduleExchangeRequest requests the battery capacity and no deadline when neither "
         "is stated") {
    const auto mode = dynamic_mode(first_request(ev::d20::SessionOptions{}, no_seed));
    REQUIRE(mode.departure_time == std::numeric_limits<std::uint32_t>::max());
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.target_energy) == Catch::Approx(80000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.max_energy) == Catch::Approx(80000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.min_energy) == Catch::Approx(0.0f));
}

SCENARIO("ISO15118-20 EV Dynamic ScheduleExchangeRequest states no deadline once the departure has passed") {
    const auto mode = dynamic_mode(first_request(with_energy_request(20000.0f, 0s), no_seed));
    REQUIRE(mode.departure_time == std::numeric_limits<std::uint32_t>::max());
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.target_energy) == Catch::Approx(20000.0f));
}

SCENARIO("ISO15118-20 EV Scheduled ScheduleExchangeRequest states start_charging's energy and departure time") {
    const auto mode = scheduled_mode(first_request(with_energy_request(20000.0f, 3600s), seed_scheduled));
    REQUIRE(mode.departure_time == 3600U);
    REQUIRE(mode.target_energy.has_value());
    REQUIRE(message_20::datatypes::from_RationalNumber(*mode.target_energy) == Catch::Approx(20000.0f));
    REQUIRE_FALSE(mode.max_energy.has_value());
    REQUIRE_FALSE(mode.min_energy.has_value());
}

SCENARIO("ISO15118-20 EV Scheduled ScheduleExchangeRequest omits a departure time that has passed") {
    const auto mode = scheduled_mode(first_request(with_energy_request(20000.0f, 0s), seed_scheduled));
    REQUIRE_FALSE(mode.departure_time.has_value());
    REQUIRE(mode.target_energy.has_value());
}

SCENARIO("ISO15118-20 EV Scheduled ScheduleExchangeRequest omits energy and departure time when neither is stated") {
    const auto mode = scheduled_mode(first_request(ev::d20::SessionOptions{}, seed_scheduled));
    REQUIRE_FALSE(mode.departure_time.has_value());
    REQUIRE_FALSE(mode.target_energy.has_value());
}

SCENARIO("ISO15118-20 EV ScheduleExchange counts the departure time down on an Ongoing re-send") {
    const ev::feedback::Callbacks callbacks{};
    const auto on_hand_clock = [](FsmStateHelper& helper) { use_hand_steady_clock(helper); };
    PrimedState<ev::d20::state::ScheduleExchange> primed{callbacks, ServiceCategory::DC,
                                                         with_energy_request(20000.0f, 3600s), on_hand_clock};
    // The first request, with 3600 s left, went out on the transition into the state.
    hand_steady_time += 90s;
    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK, Processing::Ongoing));
    REQUIRE_FALSE(primed.feed(ev::d20::Event::V2GTP_MESSAGE).transitioned());
    REQUIRE(dynamic_mode(primed.take_requests()).departure_time == 3510U);
}
