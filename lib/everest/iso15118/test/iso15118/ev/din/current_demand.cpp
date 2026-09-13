// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/din/state/current_demand.hpp>
#include <iso15118/message_din/current_demand.hpp>
#include <iso15118/message_din/power_delivery.hpp>

using namespace iso15118;

namespace {
namespace dt = message_din::datatypes;
using ev::din::StateID;
using State = ev::din::state::CurrentDemand;

const auto seed_params = [](DinStateHelper& helper) {
    ev::DcChargeParams params;
    params.target_voltage = 400.0f;
    params.target_current = 125.0f;
    params.max_voltage = 900.0f;
    params.max_charge_current = 300.0f;
    params.max_charge_power = 150000.0f;
    params.present_soc = 50.0;
    helper.set_dc_params(params);
};

message_din::CurrentDemandResponse make_response(const message_din::Header& hdr, dt::ResponseCode code,
                                                 dt::DcEvseStatusCode status_code,
                                                 dt::EvseNotification notification = dt::EvseNotification::None) {
    message_din::CurrentDemandResponse res;
    res.header = hdr;
    res.response_code = code;
    res.dc_evse_status.evse_status_code = status_code;
    res.dc_evse_status.evse_notification = notification;
    res.evse_present_voltage = 400.0;
    res.evse_present_current = 125.0;
    return res;
}

message_din::CurrentDemandResponse ok_response(const message_din::Header& hdr) {
    return make_response(hdr, dt::ResponseCode::OK, dt::DcEvseStatusCode::EVSE_Ready);
}

// Feed one response and assert the loop keeps running.
void expect_continues(PrimedState<State>& primed, const message_din::CurrentDemandResponse& res) {
    primed.handle_response(res);
    const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);
    REQUIRE(result.output == ev::din::Disposition::Awaiting);
    REQUIRE(primed.fsm.get_current_state_id() == StateID::CurrentDemand);
    REQUIRE(primed.take_requests().get<message_din::CurrentDemandRequest>().has_value());
}
} // namespace

SCENARIO("DIN 70121 EV CurrentDemand sends the targets and limits on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, seed_params};

    const auto request = primed.take_requests().get<message_din::CurrentDemandRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->dc_ev_status.ev_ready == true);
    REQUIRE(request->ev_target_voltage == 400.0);
    REQUIRE(request->ev_target_current == 125.0);
    REQUIRE(request->ev_maximum_voltage_limit.value() == 900.0);
    REQUIRE(request->ev_maximum_current_limit.value() == 300.0);
    REQUIRE(request->ev_maximum_power_limit.value() == 150000.0);
    REQUIRE(request->charging_complete == false);
}

SCENARIO("DIN 70121 EV CurrentDemand keeps charging while the EVSE is ready") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, seed_params};

    expect_continues(primed, ok_response(header()));
    REQUIRE(primed.ctx.is_session_stopped() == false);
}

SCENARIO("DIN 70121 EV CurrentDemand treats informational status codes as no reason to stop [V2G-DC-637]") {
    const ev::feedback::Callbacks callbacks{};

    SECTION("EVSE_Malfunction") {
        PrimedState<State> primed{callbacks, seed_params};
        expect_continues(primed, make_response(header(), dt::ResponseCode::OK, dt::DcEvseStatusCode::EVSE_Malfunction));
    }
    SECTION("EVSE_IsolationMonitoringActive") {
        PrimedState<State> primed{callbacks, seed_params};
        expect_continues(primed, make_response(header(), dt::ResponseCode::OK,
                                               dt::DcEvseStatusCode::EVSE_IsolationMonitoringActive));
    }
    SECTION("EVSE_NotReady") {
        PrimedState<State> primed{callbacks, seed_params};
        expect_continues(primed, make_response(header(), dt::ResponseCode::OK, dt::DcEvseStatusCode::EVSE_NotReady));
    }
}

SCENARIO("DIN 70121 EV CurrentDemand publishes the SECC limits carried by the response") {
    std::optional<ev::feedback::DcMaximumLimits> limits;
    ev::feedback::Callbacks callbacks{};
    callbacks.dc_evse_present_limits = [&limits](const ev::feedback::DcMaximumLimits& l) { limits = l; };
    PrimedState<State> primed{callbacks, seed_params};

    auto res = ok_response(header());
    res.evse_maximum_voltage_limit = 500.0;
    res.evse_maximum_current_limit = 250.0;
    res.evse_maximum_power_limit = 125000.0;
    expect_continues(primed, res);

    REQUIRE(limits.has_value());
    REQUIRE(limits->voltage == 500.0f);
    REQUIRE(limits->current == 250.0f);
    REQUIRE(limits->power == 125000.0f);
}

SCENARIO("DIN 70121 EV CurrentDemand leaves the loop when the charger requests a stop") {
    bool stopped_from_charger = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.stop_from_charger = [&stopped_from_charger]() { stopped_from_charger = true; };

    const auto run = [&](const message_din::CurrentDemandResponse& res) {
        PrimedState<State> primed{callbacks, seed_params};
        primed.handle_response(res);
        const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

        REQUIRE(result.transitioned() == true);
        REQUIRE(primed.fsm.get_current_state_id() == StateID::PowerDelivery);
        REQUIRE(stopped_from_charger == true);
        const auto request = primed.take_requests().get<message_din::PowerDeliveryRequest>();
        REQUIRE(request.has_value());
        REQUIRE(request->ready_to_charge_state == false);
        // A charger stop terminates the session rather than pausing it.
        REQUIRE(primed.ctx.stop_is_pause() == false);
    };

    SECTION("EVSENotification StopCharging") {
        run(make_response(header(), dt::ResponseCode::OK, dt::DcEvseStatusCode::EVSE_Ready,
                          dt::EvseNotification::StopCharging));
    }
    SECTION("EVSE_Shutdown") {
        run(make_response(header(), dt::ResponseCode::OK, dt::DcEvseStatusCode::EVSE_Shutdown));
    }
    SECTION("EVSE_EmergencyShutdown") {
        run(make_response(header(), dt::ResponseCode::OK, dt::DcEvseStatusCode::EVSE_EmergencyShutdown));
    }
}

SCENARIO("DIN 70121 EV CurrentDemand leaves the loop on an EV stop request") {
    bool stopped_from_charger = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.stop_from_charger = [&stopped_from_charger]() { stopped_from_charger = true; };
    PrimedState<State> primed{callbacks, seed_params};

    primed.ctx.set_stop_charging_requested(true);
    primed.handle_response(ok_response(header()));
    const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == StateID::PowerDelivery);
    REQUIRE(stopped_from_charger == false);
    REQUIRE(primed.take_requests().get<message_din::PowerDeliveryRequest>().has_value());
}

SCENARIO("DIN 70121 EV CurrentDemand leaves the loop on an EV pause request") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, seed_params};

    primed.ctx.set_pause_charging_requested(true);
    primed.handle_response(ok_response(header()));
    primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(primed.fsm.get_current_state_id() == StateID::PowerDelivery);
    // DIN has no pause on the wire: SessionStop pauses, so its session id can be re-joined.
    REQUIRE(primed.ctx.stop_is_pause() == true);
}

SCENARIO("DIN 70121 EV CurrentDemand rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](DinStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(SESSION_ID);
        return fsm::v2::FSM<ev::din::StateBase>{ctx.create_state<State>()};
    };
    message_din::PowerDeliveryResponse wrong;
    wrong.header = header();
    wrong.response_code = dt::ResponseCode::OK;
    check_rejection_paths(callbacks, StateID::CurrentDemand, make_fsm, ok_response, wrong);
}
