// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <variant>

#include <iso15118/detail/d20/ev/state/ac_charge_parameter_discovery.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;

namespace {

d20::ev::AcEvChargeParameters make_ac_params() {
    d20::ev::AcEvChargeParameters params;
    params.max_charge_power = dt::from_float(11000.0f);
    params.min_charge_power = dt::from_float(100.0f);
    return params;
}

} // namespace

SCENARIO("EVCC AC_ChargeParameterDiscovery request/response handling") {
    GIVEN("A unidirectional AC request") {
        const auto req = d20::ev::state::ac_charge_parameter_discovery::create_request(make_ac_params());
        THEN("It carries the unidirectional AC transfer mode with the configured limits") {
            REQUIRE(std::holds_alternative<dt::AC_CPDReqEnergyTransferMode>(req.transfer_mode));
            const auto& mode = std::get<dt::AC_CPDReqEnergyTransferMode>(req.transfer_mode);
            REQUIRE(dt::from_RationalNumber(mode.max_charge_power) == 11000.0f);
            REQUIRE(dt::from_RationalNumber(mode.min_charge_power) == 100.0f);
        }
    }

    GIVEN("A bidirectional (BPT) AC request with explicit discharge limits") {
        auto params = make_ac_params();
        params.max_discharge_power = dt::from_float(11000.0f);
        params.min_discharge_power = dt::from_float(100.0f);

        const auto req = d20::ev::state::ac_charge_parameter_discovery::create_bpt_request(params);
        THEN("It carries the BPT transfer mode with charge and discharge limits") {
            REQUIRE(std::holds_alternative<dt::BPT_AC_CPDReqEnergyTransferMode>(req.transfer_mode));
            const auto& mode = std::get<dt::BPT_AC_CPDReqEnergyTransferMode>(req.transfer_mode);
            REQUIRE(dt::from_RationalNumber(mode.max_charge_power) == 11000.0f);
            REQUIRE(dt::from_RationalNumber(mode.max_discharge_power) == 11000.0f);
            REQUIRE(dt::from_RationalNumber(mode.min_discharge_power) == 100.0f);
        }
    }

    GIVEN("A bidirectional (BPT) AC request without configured discharge limits") {
        const auto req = d20::ev::state::ac_charge_parameter_discovery::create_bpt_request(make_ac_params());
        THEN("The SIL discharge defaults are applied") {
            REQUIRE(std::holds_alternative<dt::BPT_AC_CPDReqEnergyTransferMode>(req.transfer_mode));
            const auto& mode = std::get<dt::BPT_AC_CPDReqEnergyTransferMode>(req.transfer_mode);
            REQUIRE(dt::from_RationalNumber(mode.max_discharge_power) == 11000.0f);
            REQUIRE(dt::from_RationalNumber(mode.min_discharge_power) == 100.0f);
        }
    }

    GIVEN("A successful unidirectional response with SECC limits") {
        message_20::AC_ChargeParameterDiscoveryResponse res;
        res.response_code = dt::ResponseCode::OK;
        auto& mode = res.transfer_mode.emplace<dt::AC_CPDResEnergyTransferMode>();
        mode.max_charge_power = dt::from_float(22000.0f);
        mode.min_charge_power = dt::from_float(100.0f);
        mode.nominal_frequency = dt::from_float(50.0f);

        const auto result = d20::ev::state::ac_charge_parameter_discovery::handle_response(res);
        THEN("It is valid and the charge-power limit is extracted (no discharge limit)") {
            REQUIRE(result.valid == true);
            REQUIRE(result.limits.charge_power == 22000.0f);
            REQUIRE(result.limits.discharge_power.has_value() == false);
        }
    }

    GIVEN("A successful bidirectional (BPT) response with SECC limits") {
        message_20::AC_ChargeParameterDiscoveryResponse res;
        res.response_code = dt::ResponseCode::OK;
        auto& mode = res.transfer_mode.emplace<dt::BPT_AC_CPDResEnergyTransferMode>();
        mode.max_charge_power = dt::from_float(22000.0f);
        mode.min_charge_power = dt::from_float(100.0f);
        mode.nominal_frequency = dt::from_float(50.0f);
        mode.max_discharge_power = dt::from_float(11000.0f);
        mode.min_discharge_power = dt::from_float(100.0f);

        const auto result = d20::ev::state::ac_charge_parameter_discovery::handle_response(res);
        THEN("It is valid and both charge and discharge power limits are extracted") {
            REQUIRE(result.valid == true);
            REQUIRE(result.limits.charge_power == 22000.0f);
            REQUIRE(result.limits.discharge_power.has_value());
            REQUIRE(result.limits.discharge_power.value() == 11000.0f);
        }
    }

    GIVEN("A FAILED_WrongChargeParameter response") {
        message_20::AC_ChargeParameterDiscoveryResponse res;
        res.response_code = dt::ResponseCode::FAILED_WrongChargeParameter;
        const auto result = d20::ev::state::ac_charge_parameter_discovery::handle_response(res);
        THEN("It is invalid (the session terminates)") {
            REQUIRE(result.valid == false);
        }
    }
}
