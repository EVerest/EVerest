// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/din/state/charge_parameter_discovery.hpp>
#include <iso15118/message_din/cable_check.hpp>
#include <iso15118/message_din/charge_parameter_discovery.hpp>
#include <iso15118/message_din/session_stop.hpp>

using namespace iso15118;

namespace {
namespace dt = message_din::datatypes;
using ev::din::StateID;
using State = ev::din::state::ChargeParameterDiscovery;

ev::DcChargeParams charge_params() {
    ev::DcChargeParams params;
    params.max_charge_current = 200.0f;
    params.max_charge_power = 150000.0f;
    params.max_voltage = 900.0f;
    params.energy_capacity = 60000.0f;
    params.present_soc = 50.0;
    return params;
}

const auto seed_params = [](DinStateHelper& helper) { helper.set_dc_params(charge_params()); };

dt::DcEvseChargeParameter evse_parameter(dt::DcEvseStatusCode status_code, dt::EvseNotification notification,
                                         std::optional<double> power_limit) {
    dt::DcEvseChargeParameter param;
    param.dc_evse_status.evse_status_code = status_code;
    param.dc_evse_status.evse_notification = notification;
    param.evse_maximum_current_limit = 300.0;
    param.evse_maximum_voltage_limit = 500.0;
    param.evse_maximum_power_limit = power_limit;
    return param;
}

message_din::ChargeParameterDiscoveryResponse make_response(const message_din::Header& hdr, dt::ResponseCode code,
                                                            dt::EvseProcessing processing,
                                                            std::optional<dt::DcEvseChargeParameter> param) {
    message_din::ChargeParameterDiscoveryResponse res;
    res.header = hdr;
    res.response_code = code;
    res.evse_processing = processing;
    res.dc_evse_charge_parameter = param;
    return res;
}

message_din::ChargeParameterDiscoveryResponse ok_response(const message_din::Header& hdr) {
    return make_response(hdr, dt::ResponseCode::OK, dt::EvseProcessing::Finished,
                         evse_parameter(dt::DcEvseStatusCode::EVSE_Ready, dt::EvseNotification::None, 120000.0));
}
} // namespace

SCENARIO("DIN 70121 EV ChargeParameterDiscovery sends the EV charge parameters on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, seed_params};

    const auto request = primed.take_requests().get<message_din::ChargeParameterDiscoveryRequest>();
    REQUIRE(request.has_value());
    REQUIRE(request->ev_requested_energy_transfer_type == dt::EnergyTransferMode::DC_extended);
    REQUIRE(request->dc_ev_charge_parameter.has_value());
    const auto& param = request->dc_ev_charge_parameter.value();
    REQUIRE(param.dc_ev_status.ev_ready == false);
    REQUIRE(param.dc_ev_status.ev_ress_soc == 50);
    REQUIRE(param.ev_maximum_current_limit == 200.0);
    REQUIRE(param.ev_maximum_voltage_limit == 900.0);
    REQUIRE(param.ev_maximum_power_limit.has_value());
    REQUIRE(param.ev_maximum_power_limit.value() == 150000.0);
    REQUIRE(param.ev_energy_capacity.has_value());
    REQUIRE_FALSE(param.ev_energy_request.has_value());
    REQUIRE_FALSE(param.full_soc.has_value());
    REQUIRE_FALSE(param.bulk_soc.has_value());
}

SCENARIO("DIN 70121 EV ChargeParameterDiscovery omits an unset EV maximum power limit") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed_no_power = [](DinStateHelper& helper) {
        auto params = charge_params();
        params.max_charge_power = 0.0f;
        helper.set_dc_params(params);
    };
    PrimedState<State> primed{callbacks, seed_no_power};

    const auto request = primed.take_requests().get<message_din::ChargeParameterDiscoveryRequest>();
    REQUIRE(request.has_value());
    REQUIRE_FALSE(request->dc_ev_charge_parameter->ev_maximum_power_limit.has_value());
}

SCENARIO("DIN 70121 EV ChargeParameterDiscovery clamps the requested energy transfer mode [V2G-DC-625]") {
    const ev::feedback::Callbacks callbacks{};

    SECTION("an AC request becomes DC_extended") {
        ev::EvSessionParams params;
        params.energy_transfer_mode = dt::EnergyTransferMode::AC_three_phase_core;
        PrimedState<State> primed{callbacks, params, false, seed_params};
        const auto request = primed.take_requests().get<message_din::ChargeParameterDiscoveryRequest>();
        REQUIRE(request->ev_requested_energy_transfer_type == dt::EnergyTransferMode::DC_extended);
    }
    SECTION("DC_core is kept") {
        ev::EvSessionParams params;
        params.energy_transfer_mode = dt::EnergyTransferMode::DC_core;
        PrimedState<State> primed{callbacks, params, false, seed_params};
        const auto request = primed.take_requests().get<message_din::ChargeParameterDiscoveryRequest>();
        REQUIRE(request->ev_requested_energy_transfer_type == dt::EnergyTransferMode::DC_core);
    }
}

SCENARIO("DIN 70121 EV ChargeParameterDiscovery stays and re-polls on Ongoing") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, seed_params};

    primed.handle_response(make_response(header(), dt::ResponseCode::OK, dt::EvseProcessing::Ongoing, std::nullopt));
    const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(result.output == ev::din::Disposition::Awaiting);
    REQUIRE(primed.fsm.get_current_state_id() == StateID::ChargeParameterDiscovery);
    REQUIRE(primed.take_requests().get<message_din::ChargeParameterDiscoveryRequest>().has_value());
}

SCENARIO("DIN 70121 EV ChargeParameterDiscovery publishes the EVSE limits and advances to CableCheck") {
    bool power_ready = false;
    std::optional<ev::feedback::DcMaximumLimits> limits;
    ev::feedback::Callbacks callbacks{};
    callbacks.ev_power_ready = [&power_ready]() { power_ready = true; };
    callbacks.dc_evse_present_limits = [&limits](const ev::feedback::DcMaximumLimits& l) { limits = l; };
    PrimedState<State> primed{callbacks, seed_params};

    primed.handle_response(ok_response(header()));
    const auto result = primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == StateID::CableCheck);
    REQUIRE(power_ready == true);
    REQUIRE(limits.has_value());
    REQUIRE(limits->current == 300.0f);
    REQUIRE(limits->voltage == 500.0f);
    REQUIRE(limits->power == 120000.0f);
    REQUIRE(primed.ctx.evse_info.dc_present_limits.has_value());
    REQUIRE(primed.take_requests().get<message_din::CableCheckRequest>().has_value());
}

SCENARIO("DIN 70121 EV ChargeParameterDiscovery derives the power limit when the SECC omits it") {
    std::optional<ev::feedback::DcMaximumLimits> limits;
    ev::feedback::Callbacks callbacks{};
    callbacks.dc_evse_present_limits = [&limits](const ev::feedback::DcMaximumLimits& l) { limits = l; };
    PrimedState<State> primed{callbacks, seed_params};

    primed.handle_response(
        make_response(header(), dt::ResponseCode::OK, dt::EvseProcessing::Finished,
                      evse_parameter(dt::DcEvseStatusCode::EVSE_Ready, dt::EvseNotification::None, std::nullopt)));
    primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(limits.has_value());
    REQUIRE(limits->power == 150000.0f);
}

SCENARIO("DIN 70121 EV ChargeParameterDiscovery diverts to SessionStop when the SECC signals no energy") {
    bool power_ready = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.ev_power_ready = [&power_ready]() { power_ready = true; };

    SECTION("EVSENotification StopCharging") {
        PrimedState<State> primed{callbacks, seed_params};
        primed.handle_response(make_response(
            header(), dt::ResponseCode::OK, dt::EvseProcessing::Finished,
            evse_parameter(dt::DcEvseStatusCode::EVSE_Ready, dt::EvseNotification::StopCharging, 120000.0)));
        primed.feed(ev::din::Event::V2GTP_MESSAGE);

        REQUIRE(primed.fsm.get_current_state_id() == StateID::SessionStop);
        REQUIRE(power_ready == false);
        REQUIRE(primed.take_requests().get<message_din::SessionStopRequest>().has_value());
    }
    SECTION("a shutdown status code") {
        PrimedState<State> primed{callbacks, seed_params};
        primed.handle_response(
            make_response(header(), dt::ResponseCode::OK, dt::EvseProcessing::Finished,
                          evse_parameter(dt::DcEvseStatusCode::EVSE_Shutdown, dt::EvseNotification::None, 120000.0)));
        primed.feed(ev::din::Event::V2GTP_MESSAGE);

        REQUIRE(primed.fsm.get_current_state_id() == StateID::SessionStop);
        REQUIRE(power_ready == false);
    }
}

SCENARIO("DIN 70121 EV ChargeParameterDiscovery keeps charging on EVSE_Malfunction [V2G-DC-637]") {
    bool power_ready = false;
    ev::feedback::Callbacks callbacks{};
    callbacks.ev_power_ready = [&power_ready]() { power_ready = true; };
    PrimedState<State> primed{callbacks, seed_params};

    primed.handle_response(
        make_response(header(), dt::ResponseCode::OK, dt::EvseProcessing::Finished,
                      evse_parameter(dt::DcEvseStatusCode::EVSE_Malfunction, dt::EvseNotification::None, 120000.0)));
    primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(primed.fsm.get_current_state_id() == StateID::CableCheck);
    REQUIRE(power_ready == true);
}

SCENARIO("DIN 70121 EV ChargeParameterDiscovery diverts to SessionStop on a latched stop request") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<State> primed{callbacks, seed_params};

    primed.ctx.set_stop_charging_requested(true);
    primed.handle_response(ok_response(header()));
    primed.feed(ev::din::Event::V2GTP_MESSAGE);

    REQUIRE(primed.fsm.get_current_state_id() == StateID::SessionStop);
    REQUIRE(primed.take_requests().get<message_din::SessionStopRequest>().has_value());
}

SCENARIO("DIN 70121 EV ChargeParameterDiscovery rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](DinStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.set_session_id(SESSION_ID);
        return fsm::v2::FSM<ev::din::StateBase>{ctx.create_state<State>()};
    };
    message_din::CableCheckResponse wrong;
    wrong.header = header();
    wrong.response_code = dt::ResponseCode::OK;
    check_rejection_paths(callbacks, StateID::ChargeParameterDiscovery, make_fsm, ok_response, wrong);
}
