// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d2/state/pre_charge.hpp>
#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/pre_charge.hpp>
#include <iso15118/message_2/session_stop.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

namespace {

message_2::PreChargeResponse make_response(const message_2::Header& header, dt::ResponseCode code,
                                           double present_voltage) {
    message_2::PreChargeResponse res;
    res.header = header;
    res.response_code = code;
    res.evse_present_voltage = dt::to_physical_value(present_voltage, dt::Unit::V);
    return res;
}

} // namespace

SCENARIO("ISO15118-2 EV PreCharge sends the target voltage at zero current") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::PreCharge> primed{callbacks, d2_no_seed};

    const auto request = primed.take_requests().get<message_2::PreChargeRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->header.session_id == D2_SESSION_ID);
    REQUIRE(dt::from_physical_value(request->ev_target_voltage) == 400.0);
    REQUIRE(dt::from_physical_value(request->ev_target_current) == 0.0);
    REQUIRE(request->dc_ev_status.ev_ready == true);
}

SCENARIO("ISO15118-2 EV PreCharge resends until the voltage converges") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d2::state::PreCharge> primed{callbacks, d2_no_seed};

    REQUIRE(primed.helper.get_message_exchange().take_request().has_value());
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, 100.0));
    const auto result = primed.feed(ev::d2::Event::V2GTP_MESSAGE);

    REQUIRE(result.output == ev::d2::Disposition::Awaiting);
    REQUIRE(primed.take_requests().get<message_2::PreChargeRequest>().has_value());
}

SCENARIO("ISO15118-2 EV PreCharge publishes dc_power_on and starts power delivery on convergence") {
    bool power_on = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.dc_power_on = [&power_on]() { power_on = true; };
    PrimedState<ev::d2::state::PreCharge> primed{callbacks, d2_no_seed};

    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, 395.0));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);

    REQUIRE(power_on);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::PowerDelivery);
}

SCENARIO("ISO15118-2 EV PreCharge does not power on when a stop is latched") {
    bool power_on = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.dc_power_on = [&power_on]() { power_on = true; };
    PrimedState<ev::d2::state::PreCharge> primed{callbacks, d2_no_seed};

    primed.ctx.set_stop_charging_requested(true);
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, 395.0));
    REQUIRE(primed.feed(ev::d2::Event::V2GTP_MESSAGE).transitioned() == true);

    REQUIRE_FALSE(power_on);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d2::StateID::SessionStop);
}

SCENARIO("ISO15118-2 EV PreCharge never converges on a non-positive target voltage") {
    bool power_on = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.dc_power_on = [&power_on]() { power_on = true; };
    const auto seed_zero_target = [](D2StateHelper& helper) {
        auto params = default_dc_params();
        params.target_voltage = 0.0f;
        helper.set_dc_params(params);
    };
    PrimedState<ev::d2::state::PreCharge> primed{callbacks, ev::EvSessionParams{}, false, seed_zero_target};

    REQUIRE(primed.helper.get_message_exchange().take_request().has_value());
    primed.handle_response(make_response(d2_header(), dt::ResponseCode::OK, 0.0));
    const auto result = primed.feed(ev::d2::Event::V2GTP_MESSAGE);

    REQUIRE(result.output == ev::d2::Disposition::Awaiting);
    REQUIRE_FALSE(power_on);
    REQUIRE(primed.take_requests().get<message_2::PreChargeRequest>().has_value());
}

SCENARIO("ISO15118-2 EV PreCharge rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](D2StateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(D2_SESSION_ID);
        return fsm::v2::FSM<ev::d2::StateBase>{ctx.create_state<ev::d2::state::PreCharge>()};
    };
    const auto make_ok = [](const message_2::Header& header) {
        return make_response(header, dt::ResponseCode::OK, 400.0);
    };
    message_2::AuthorizationResponse wrong;
    wrong.header = d2_header();
    wrong.response_code = dt::ResponseCode::OK;
    wrong.evse_processing = dt::EVSEProcessing::Finished;
    check_rejection_paths(callbacks, ev::d2::StateID::PreCharge, make_fsm, make_ok, wrong);
}
