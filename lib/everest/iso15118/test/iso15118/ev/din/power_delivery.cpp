// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/din/state/power_delivery.hpp>
#include <iso15118/message_din/current_demand.hpp>
#include <iso15118/message_din/power_delivery.hpp>
#include <iso15118/message_din/welding_detection.hpp>

using namespace iso15118;

namespace {
namespace dt = message_din::datatypes;
using ev::din::StateID;
using State = ev::din::state::PowerDelivery;

const auto seed_charging = [](DinStateHelper& helper) {
    ev::DcChargeParams params;
    params.present_soc = 50.0;
    helper.set_dc_params(params);
};

const auto seed_full = [](DinStateHelper& helper) {
    ev::DcChargeParams params;
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
} // namespace

SCENARIO("DIN 70121 EV PowerDelivery sends ReadyToChargeState per phase") {
    const ev::feedback::Callbacks callbacks{};

    SECTION("Start") {
        PrimedState<State> primed{callbacks, seed_charging, State::Phase::Start};
        const auto request = primed.take_requests().get<message_din::PowerDeliveryRequest>();
        REQUIRE(request.has_value());
        REQUIRE(request->ready_to_charge_state == true);
        REQUIRE(request->dc_ev_power_delivery_parameter.has_value());
        REQUIRE(request->dc_ev_power_delivery_parameter->dc_ev_status.ev_ready == true);
        REQUIRE(request->dc_ev_power_delivery_parameter->charging_complete == false);
    }
    SECTION("Stop") {
        PrimedState<State> primed{callbacks, seed_charging, State::Phase::Stop};
        const auto request = primed.take_requests().get<message_din::PowerDeliveryRequest>();
        REQUIRE(request.has_value());
        REQUIRE(request->ready_to_charge_state == false);
    }
}

SCENARIO("DIN 70121 EV PowerDelivery reports ChargingComplete at a full battery") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, seed_full, State::Phase::Stop};

    const auto request = primed.take_requests().get<message_din::PowerDeliveryRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->dc_ev_power_delivery_parameter->charging_complete == true);
}

SCENARIO("DIN 70121 EV PowerDelivery(Start) transitions to CurrentDemand") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, seed_charging, State::Phase::Start};

    primed.handle_response(ok_response(header()));
    const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == StateID::CurrentDemand);
    REQUIRE(primed.take_requests().get<message_din::CurrentDemandRequest>().has_value());
}

SCENARIO("DIN 70121 EV PowerDelivery(Stop) transitions to WeldingDetection") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, seed_charging, State::Phase::Stop};

    primed.handle_response(ok_response(header()));
    const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == StateID::WeldingDetection);
    REQUIRE(primed.take_requests().get<message_din::WeldingDetectionRequest>().has_value());
}

SCENARIO("DIN 70121 EV PowerDelivery rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](DinStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(SESSION_ID);
        return fsm::v2::FSM<ev::din::StateBase>{ctx.create_state<State>(State::Phase::Start)};
    };
    message_din::CurrentDemandResponse wrong;
    wrong.header = header();
    wrong.response_code = dt::ResponseCode::OK;
    check_rejection_paths(callbacks, StateID::PowerDelivery, make_fsm, ok_response, wrong);
}
