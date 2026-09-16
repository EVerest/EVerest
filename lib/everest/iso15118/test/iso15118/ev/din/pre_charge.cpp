// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/din/state/pre_charge.hpp>
#include <iso15118/message_din/power_delivery.hpp>
#include <iso15118/message_din/pre_charge.hpp>
#include <iso15118/message_din/session_stop.hpp>

using namespace iso15118;

namespace {
namespace dt = message_din::datatypes;
using ev::din::StateID;
using State = ev::din::state::PreCharge;

const auto seed_params = [](DinStateHelper& helper) {
    ev::DcChargeParams params;
    params.target_voltage = 400.0f;
    params.present_soc = 50.0;
    helper.set_dc_params(params);
};

message_din::PreChargeResponse make_response(const message_din::Header& hdr, dt::ResponseCode code,
                                             double present_voltage) {
    message_din::PreChargeResponse res;
    res.header = hdr;
    res.response_code = code;
    res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
    res.evse_present_voltage = present_voltage;
    return res;
}

message_din::PreChargeResponse ok_response(const message_din::Header& hdr) {
    return make_response(hdr, dt::ResponseCode::OK, 395.0);
}
} // namespace

SCENARIO("DIN 70121 EV PreCharge sends the target voltage at zero current on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, seed_params};

    const auto request = primed.take_requests().get<message_din::PreChargeRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->dc_ev_status.ev_ready == true);
    REQUIRE(request->ev_target_voltage == 400.0);
    REQUIRE(request->ev_target_current == 0.0);
}

SCENARIO("DIN 70121 EV PreCharge advances to PowerDelivery once the voltage converged") {
    bool power_on = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.dc_power_on = [&power_on]() { power_on = true; };

    SECTION("inside the +/- 10 % band") {
        PrimedState<State> primed{callbacks, seed_params};
        primed.handle_response(make_response(header(), dt::ResponseCode::OK, 395.0));
        const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

        REQUIRE(result.transitioned() == true);
        REQUIRE(primed.fsm.get_current_state_id() == StateID::PowerDelivery);
        REQUIRE(power_on == true);
        const auto request = primed.take_requests().get<message_din::PowerDeliveryRequest>();
        REQUIRE(request.has_value());
        REQUIRE(request->ready_to_charge_state == true);
    }
    SECTION("exactly at the 20 V absolute cap") {
        PrimedState<State> primed{callbacks, seed_params};
        primed.handle_response(make_response(header(), dt::ResponseCode::OK, 420.0));
        primed.feed(ev::din::Event::V2GTP_MESSAGE);

        REQUIRE(primed.fsm.get_current_state_id() == StateID::PowerDelivery);
        REQUIRE(power_on == true);
    }
}

SCENARIO("DIN 70121 EV PreCharge keeps ramping while the voltage has not converged") {
    bool power_on = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.dc_power_on = [&power_on]() { power_on = true; };

    SECTION("inside the 10 % band but beyond the 20 V absolute cap") {
        PrimedState<State> primed{callbacks, seed_params};
        primed.handle_response(make_response(header(), dt::ResponseCode::OK, 425.0));
        const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

        REQUIRE(result.output == ev::din::Disposition::Awaiting);
        REQUIRE(primed.fsm.get_current_state_id() == StateID::PreCharge);
        REQUIRE(power_on == false);
        REQUIRE(primed.take_requests().get<message_din::PreChargeRequest>().has_value());
    }
    SECTION("outside the 10 % band") {
        PrimedState<State> primed{callbacks, seed_params};
        primed.handle_response(make_response(header(), dt::ResponseCode::OK, 300.0));
        const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

        REQUIRE(result.output == ev::din::Disposition::Awaiting);
        REQUIRE(power_on == false);
    }
    SECTION("a non-positive target voltage never converges") {
        const auto seed_zero_target = [](DinStateHelper& helper) {
            ev::DcChargeParams params;
            params.target_voltage = 0.0f;
            params.present_soc = 50.0;
            helper.set_dc_params(params);
        };
        PrimedState<State> primed{callbacks, seed_zero_target};
        primed.handle_response(make_response(header(), dt::ResponseCode::OK, 0.0));
        const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

        REQUIRE(result.output == ev::din::Disposition::Awaiting);
        REQUIRE(power_on == false);
        REQUIRE(primed.take_requests().get<message_din::PreChargeRequest>().has_value());
    }
}

SCENARIO("DIN 70121 EV PreCharge diverts to SessionStop without publishing dc_power_on") {
    bool power_on = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.dc_power_on = [&power_on]() { power_on = true; };
    PrimedState<State> primed{callbacks, seed_params};

    primed.ctx.set_stop_charging_requested(true);
    primed.handle_response(ok_response(header()));
    primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(primed.fsm.get_current_state_id() == StateID::SessionStop);
    REQUIRE(power_on == false);
    REQUIRE(primed.take_requests().get<message_din::SessionStopRequest>().has_value());
}

SCENARIO("DIN 70121 EV PreCharge rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](DinStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(SESSION_ID);
        return fsm::v2::FSM<ev::din::StateBase>{ctx.create_state<State>()};
    };
    message_din::PowerDeliveryResponse wrong;
    wrong.header = header();
    wrong.response_code = dt::ResponseCode::OK;
    check_rejection_paths(callbacks, StateID::PreCharge, make_fsm, ok_response, wrong);
}
