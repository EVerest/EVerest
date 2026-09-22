// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "helper.hpp"

#include <iso15118/ev/din/state/current_demand.hpp>
#include <iso15118/ev/din/state/power_delivery.hpp>
#include <iso15118/ev/din/state/pre_charge.hpp>
#include <iso15118/message_din/current_demand.hpp>
#include <iso15118/message_din/power_delivery.hpp>
#include <iso15118/message_din/pre_charge.hpp>
#include <iso15118/message_din/welding_detection.hpp>

using namespace iso15118;

namespace {
namespace dt = message_din::datatypes;
using ev::din::StateID;
using State = ev::din::state::PowerDelivery;

const auto seed_charging = [](DinStateHelper& helper) {
    ev::params::DcChargeParams params;
    params.target_voltage = 400.0f;
    params.present_soc = 50.0;
    helper.set_dc_params(params);
};

const auto seed_full = [](DinStateHelper& helper) {
    ev::params::DcChargeParams params;
    params.present_soc = 100.0;
    helper.set_dc_params(params);
};

message_din::PowerDeliveryResponse ok_response(const message_din::Header& hdr) {
    message_din::PowerDeliveryResponse res;
    res.header = hdr;
    res.response_code = dt::ResponseCode::OK;
    dt::DcEvseStatus status;
    status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
    res.dc_evse_status = status;
    return res;
}
const auto by_pre_charge = entered_by([](DinStateHelper& helper) {
    message_din::PreChargeResponse res{};
    res.header = header();
    res.response_code = dt::ResponseCode::OK;
    res.evse_present_voltage = 400.0;
    return successor_of<ev::din::state::PreCharge>(helper, res);
});

// The SECC stopping the charge loop leaves through PowerDelivery(Stop).
const auto by_current_demand = entered_by([](DinStateHelper& helper) {
    message_din::CurrentDemandResponse res{};
    res.header = header();
    res.response_code = dt::ResponseCode::OK;
    res.dc_evse_status.evse_notification = dt::EvseNotification::StopCharging;
    return successor_of<ev::din::state::CurrentDemand>(helper, res);
});

} // namespace

SCENARIO("DIN 70121 EV PowerDelivery's transition sends ReadyToChargeState per phase") {
    const ev::feedback::Callbacks callbacks{};

    SECTION("Start") {
        PrimedState<State> primed{callbacks, seed_charging, by_pre_charge};
        const auto request = primed.take_requests().get<message_din::PowerDeliveryRequest>();
        REQUIRE(request.has_value());
        REQUIRE(request->ready_to_charge_state == true);
        REQUIRE(request->dc_ev_power_delivery_parameter.has_value());
        REQUIRE(request->dc_ev_power_delivery_parameter->dc_ev_status.ev_ready == true);
        REQUIRE(request->dc_ev_power_delivery_parameter->charging_complete == false);
    }
    SECTION("Stop") {
        PrimedState<State> primed{callbacks, seed_charging, by_current_demand};
        const auto request = primed.take_requests().get<message_din::PowerDeliveryRequest>();
        REQUIRE(request.has_value());
        REQUIRE(request->ready_to_charge_state == false);
        // EVReady means the EV is ready to charge, so a stop request must not carry it set
        // alongside ReadyToChargeState=FALSE.
        REQUIRE(request->dc_ev_power_delivery_parameter.has_value());
        REQUIRE(request->dc_ev_power_delivery_parameter->dc_ev_status.ev_ready == false);
    }
}

SCENARIO("DIN 70121 EV PowerDelivery reports ChargingComplete at a full battery") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, seed_full, by_current_demand};

    const auto request = primed.take_requests().get<message_din::PowerDeliveryRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->dc_ev_power_delivery_parameter->charging_complete == true);
}

SCENARIO("DIN 70121 EV PowerDelivery(Start) transitions to CurrentDemand") {
    int stopped = 0;
    ev::feedback::Callbacks callbacks{};
    callbacks.power_delivery_stopped = [&] { ++stopped; };
    PrimedState<State> primed{callbacks, seed_charging, State::Start{}};

    primed.handle_response(ok_response(header()));
    const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == StateID::CurrentDemand);
    REQUIRE(primed.take_requests().get<message_din::CurrentDemandRequest>().has_value());
    REQUIRE(stopped == 0);
}

SCENARIO("DIN 70121 EV PowerDelivery(Stop) transitions to WeldingDetection") {
    int stopped = 0;
    DinStateHelper* active_helper = nullptr;
    ev::feedback::Callbacks callbacks{};
    callbacks.power_delivery_stopped = [&] {
        REQUIRE(active_helper != nullptr);
        REQUIRE_FALSE(active_helper->get_message_exchange().has_request());
        ++stopped;
    };
    PrimedState<State> primed{callbacks, seed_charging, State::Stop{ev::session::StopReason::EvStop}};

    REQUIRE(stopped == 0);
    active_helper = &primed.helper;
    primed.handle_response(ok_response(header()));
    const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(stopped == 1);
    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == StateID::WeldingDetection);
    REQUIRE(primed.take_requests().get<message_din::WeldingDetectionRequest>().has_value());
}

SCENARIO("DIN 70121 EV PowerDelivery rejects malformed responses") {
    int stopped = 0;
    ev::feedback::Callbacks callbacks{};
    callbacks.power_delivery_stopped = [&] { ++stopped; };
    const auto phase = GENERATE(as<State::Phase>{}, State::Start{}, State::Stop{ev::session::StopReason::EvStop});
    const auto make_fsm = [phase](DinStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(SESSION_ID);
        return fsm::v2::FSM<ev::din::StateBase>{ctx.create_state<State>(phase)};
    };
    message_din::CurrentDemandResponse wrong;
    wrong.header = header();
    wrong.response_code = dt::ResponseCode::OK;
    check_rejection_paths(callbacks, StateID::PowerDelivery, make_fsm, ok_response, wrong);
    REQUIRE(stopped == 0);
}
