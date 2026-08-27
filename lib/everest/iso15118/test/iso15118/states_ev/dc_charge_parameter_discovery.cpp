// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <variant>

#include <iso15118/detail/d20/ev/state/dc_charge_parameter_discovery.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;

namespace {

d20::ev::DcEvChargeParameters make_dc_params() {
    d20::ev::DcEvChargeParameters params;
    params.max_charge_power = dt::from_float(150000.0f);
    params.min_charge_power = dt::from_float(100.0f);
    params.max_charge_current = dt::from_float(300.0f);
    params.min_charge_current = dt::from_float(10.0f);
    params.max_voltage = dt::from_float(900.0f);
    params.min_voltage = dt::from_float(10.0f);
    params.target_voltage = dt::from_float(400.0f);
    params.target_current = dt::from_float(20.0f);
    params.energy_capacity = dt::from_float(60000.0f);
    return params;
}

} // namespace

SCENARIO("EVCC DC_ChargeParameterDiscovery request/response handling") {
    GIVEN("A unidirectional DC request") {
        const auto req = d20::ev::state::dc_charge_parameter_discovery::create_request(make_dc_params());
        THEN("It carries the unidirectional DC transfer mode with the configured limits") {
            REQUIRE(std::holds_alternative<dt::DC_CPDReqEnergyTransferMode>(req.transfer_mode));
            const auto& mode = std::get<dt::DC_CPDReqEnergyTransferMode>(req.transfer_mode);
            REQUIRE(dt::from_RationalNumber(mode.max_charge_power) == 150000.0f);
            REQUIRE(dt::from_RationalNumber(mode.min_charge_power) == 100.0f);
            REQUIRE(dt::from_RationalNumber(mode.max_charge_current) == 300.0f);
            REQUIRE(dt::from_RationalNumber(mode.min_charge_current) == 10.0f);
            REQUIRE(dt::from_RationalNumber(mode.max_voltage) == 900.0f);
            REQUIRE(dt::from_RationalNumber(mode.min_voltage) == 10.0f);
        }
    }

    GIVEN("A bidirectional (BPT) DC request") {
        d20::ev::DcEvBptChargeParameters bpt;
        static_cast<d20::ev::DcEvChargeParameters&>(bpt) = make_dc_params();
        bpt.max_discharge_power = dt::from_float(11000.0f);
        bpt.min_discharge_power = dt::from_float(1000.0f);
        bpt.max_discharge_current = dt::from_float(25.0f);
        bpt.min_discharge_current = dt::from_float(0.0f);

        const auto req = d20::ev::state::dc_charge_parameter_discovery::create_bpt_request(bpt);
        THEN("It carries the BPT transfer mode with charge and discharge limits") {
            REQUIRE(std::holds_alternative<dt::BPT_DC_CPDReqEnergyTransferMode>(req.transfer_mode));
            const auto& mode = std::get<dt::BPT_DC_CPDReqEnergyTransferMode>(req.transfer_mode);
            REQUIRE(dt::from_RationalNumber(mode.max_charge_power) == 150000.0f);
            REQUIRE(dt::from_RationalNumber(mode.max_discharge_power) == 11000.0f);
            REQUIRE(dt::from_RationalNumber(mode.min_discharge_power) == 1000.0f);
            REQUIRE(dt::from_RationalNumber(mode.max_discharge_current) == 25.0f);
        }
    }

    GIVEN("A successful response with SECC limits") {
        message_20::DC_ChargeParameterDiscoveryResponse res;
        res.response_code = dt::ResponseCode::OK;
        auto& mode = res.transfer_mode.emplace<dt::DC_CPDResEnergyTransferMode>();
        mode.max_charge_power = dt::from_float(360000.0f);
        mode.max_charge_current = dt::from_float(400.0f);
        mode.max_voltage = dt::from_float(920.0f);

        const auto result = d20::ev::state::dc_charge_parameter_discovery::handle_response(res);
        THEN("It is valid and the present limits are extracted") {
            REQUIRE(result.valid == true);
            REQUIRE(result.limits.power == 360000.0f);
            REQUIRE(result.limits.current == 400.0f);
            REQUIRE(result.limits.voltage == 920.0f);
        }
    }

    GIVEN("A FAILED_WrongChargeParameter response") {
        message_20::DC_ChargeParameterDiscoveryResponse res;
        res.response_code = dt::ResponseCode::FAILED_WrongChargeParameter;
        const auto result = d20::ev::state::dc_charge_parameter_discovery::handle_response(res);
        THEN("It is invalid (the session terminates)") {
            REQUIRE(result.valid == false);
        }
    }
}
