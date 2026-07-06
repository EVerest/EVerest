// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/state/ac_der_iec_charge_parameter_discovery.hpp>
#include <iso15118/message/ac_der_iec_charge_parameter_discovery.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/schedule_exchange.hpp>
#include <iso15118/message/type.hpp>

using namespace iso15118;

namespace {
using message_20::datatypes::ResponseCode;

message_20::DER_AC_ChargeParameterDiscoveryResponse make_response(const message_20::Header& header, ResponseCode code) {
    message_20::DER_AC_ChargeParameterDiscoveryResponse res{};
    res.header = header;
    res.response_code = code;
    auto& mode = res.transfer_mode;
    mode.max_charge_power = message_20::datatypes::from_float(15000.0f);
    mode.min_charge_power = message_20::datatypes::from_float(500.0f);
    mode.nominal_frequency = message_20::datatypes::from_float(50.0f);
    mode.nominal_charge_power = message_20::datatypes::from_float(15000.0f);
    mode.nominal_discharge_power = message_20::datatypes::from_float(11000.0f);
    mode.max_discharge_power = message_20::datatypes::from_float(11000.0f);
    return res;
}

// AC_DER_IEC_ChargeParameterDiscovery builds its request from the EV's AC charge params.
const auto seed_single_phase = [](FsmStateHelper& helper) {
    ev::AcChargeParams p{};
    p.max_charge_power = 22000.0f;
    p.min_charge_power = 1000.0f;
    p.three_phase = false;
    helper.set_ac_params(p);
};

const auto seed_three_phase = [](FsmStateHelper& helper) {
    ev::AcChargeParams p{};
    p.max_charge_power = 22000.0f;
    p.min_charge_power = 1000.0f;
    p.three_phase = true;
    helper.set_ac_params(p);
};
} // namespace

SCENARIO("ISO15118-20 EV AC_DER_IEC_ChargeParameterDiscovery emits a single-phase DER request on enter") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::AC_DER_IEC_ChargeParameterDiscovery> primed{callbacks, seed_single_phase};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::DER_AC_ChargeParameterDiscoveryRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->header.session_id == SESSION_HEADER.session_id);

    const auto& mode = request_message->transfer_mode;
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.max_charge_power) == Catch::Approx(22000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.min_charge_power) == Catch::Approx(1000.0f));
    // A DER session advertises discharge capability alongside charge.
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.max_discharge_power) == Catch::Approx(22000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(mode.min_discharge_power) == Catch::Approx(1000.0f));
    // The EV drives a single discovery round: processing is Finished.
    REQUIRE(mode.processing == message_20::datatypes::Processing::Finished);
    // Single-phase: no per-phase limits.
    REQUIRE_FALSE(mode.max_charge_power_L2.has_value());
    REQUIRE_FALSE(mode.max_charge_power_L3.has_value());
    REQUIRE_FALSE(mode.min_charge_power_L2.has_value());
    REQUIRE_FALSE(mode.min_charge_power_L3.has_value());
}

SCENARIO("ISO15118-20 EV AC_DER_IEC_ChargeParameterDiscovery emits per-phase limits when three_phase") {
    const ev::feedback::Callbacks callbacks{};
    PrimedState<ev::d20::state::AC_DER_IEC_ChargeParameterDiscovery> primed{callbacks, seed_three_phase};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::DER_AC_ChargeParameterDiscoveryRequest>();
    REQUIRE(request_message.has_value());

    const auto& mode = request_message->transfer_mode;
    REQUIRE(mode.max_charge_power_L2.has_value());
    REQUIRE(mode.max_charge_power_L3.has_value());
    REQUIRE(mode.min_charge_power_L2.has_value());
    REQUIRE(mode.min_charge_power_L3.has_value());
    REQUIRE(message_20::datatypes::from_RationalNumber(*mode.max_charge_power_L2) == Catch::Approx(22000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(*mode.min_charge_power_L3) == Catch::Approx(1000.0f));
    // Discharge capability is advertised per phase too.
    REQUIRE(mode.max_discharge_power_L2.has_value());
    REQUIRE(mode.max_discharge_power_L3.has_value());
    REQUIRE(mode.min_discharge_power_L2.has_value());
    REQUIRE(mode.min_discharge_power_L3.has_value());
    REQUIRE(message_20::datatypes::from_RationalNumber(*mode.max_discharge_power_L2) == Catch::Approx(22000.0f));
    REQUIRE(message_20::datatypes::from_RationalNumber(*mode.min_discharge_power_L3) == Catch::Approx(1000.0f));
}

SCENARIO("ISO15118-20 EV AC_DER_IEC_ChargeParameterDiscovery transitions to ScheduleExchange and fires ac_limits") {
    bool fired = false;
    float reported_frequency = 0.0f;
    float reported_max_charge_power = 0.0f;
    ev::feedback::Callbacks callbacks{};
    callbacks.ac_limits = [&](const message_20::datatypes::AC_CPDResEnergyTransferMode& mode) {
        fired = true;
        reported_frequency = message_20::datatypes::from_RationalNumber(mode.nominal_frequency);
        reported_max_charge_power = message_20::datatypes::from_RationalNumber(mode.max_charge_power);
    };
    PrimedState<ev::d20::state::AC_DER_IEC_ChargeParameterDiscovery> primed{callbacks, seed_single_phase};

    primed.handle_response(make_response(SESSION_HEADER, ResponseCode::OK));
    const auto result = primed.feed(ev::d20::Event::V2GTP_MESSAGE);

    REQUIRE(result.transitioned() == true);
    REQUIRE(primed.fsm.get_current_state_id() == ev::d20::StateID::ScheduleExchange);
    REQUIRE(primed.ctx.is_session_stopped() == false);
    REQUIRE(fired == true);
    REQUIRE(reported_frequency == 50.0f);
    REQUIRE(reported_max_charge_power == 15000.0f);
}

SCENARIO("ISO15118-20 EV AC_DER_IEC_ChargeParameterDiscovery rejects malformed responses") {
    const ev::feedback::Callbacks callbacks{};
    const auto make_fsm = [](FsmStateHelper& helper) {
        auto& ctx = helper.get_context();
        ctx.get_session().set_id(SESSION_HEADER.session_id);
        return fsm::v2::FSM<ev::d20::StateBase>{
            ctx.create_state<ev::d20::state::AC_DER_IEC_ChargeParameterDiscovery>()};
    };
    const auto make_ok = [](const message_20::Header& header) { return make_response(header, ResponseCode::OK); };
    check_rejection_paths(callbacks, ev::d20::StateID::AC_DER_IEC_ChargeParameterDiscovery, make_fsm, make_ok,
                          message_20::ScheduleExchangeResponse{});
}
