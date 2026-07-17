// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <variant>

#include <iso15118/detail/d20/ev/state/dc_charge_loop.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;

namespace {

d20::ev::state::dc_charge_loop::RequestParams make_params() {
    d20::ev::state::dc_charge_loop::RequestParams p;
    p.present_voltage = dt::from_float(400.0f);
    p.display.present_soc = 42;
    p.display.charging_complete = false;
    p.target_energy_request = dt::from_float(60000.0f);
    p.max_energy_request = dt::from_float(60000.0f);
    p.min_energy_request = dt::from_float(1.0f);
    p.max_charge_power = dt::from_float(150000.0f);
    p.min_charge_power = dt::from_float(0.0f);
    p.max_charge_current = dt::from_float(300.0f);
    p.max_voltage = dt::from_float(900.0f);
    p.min_voltage = dt::from_float(150.0f);
    p.target_current = dt::from_float(20.0f);
    p.target_voltage = dt::from_float(400.0f);
    p.max_discharge_power = dt::from_float(11000.0f);
    p.min_discharge_power = dt::from_float(300.0f);
    p.max_discharge_current = dt::from_float(25.0f);
    return p;
}

} // namespace

SCENARIO("EVCC DC_ChargeLoop request/response handling") {
    GIVEN("A dynamic-mode request") {
        const auto req = d20::ev::state::dc_charge_loop::create_dynamic_request(make_params(), false);
        THEN("It carries the Dynamic DC control mode with the energy window and limits") {
            REQUIRE(std::holds_alternative<dt::Dynamic_DC_CLReqControlMode>(req.control_mode));
            const auto& mode = std::get<dt::Dynamic_DC_CLReqControlMode>(req.control_mode);
            REQUIRE(dt::from_RationalNumber(mode.target_energy_request) == 60000.0f);
            REQUIRE(dt::from_RationalNumber(mode.min_charge_power) == 0.0f);
            REQUIRE(dt::from_RationalNumber(mode.min_voltage) == 150.0f);
            REQUIRE(req.meter_info_requested == false);
            REQUIRE(req.display_parameters.has_value());
            REQUIRE(req.display_parameters->present_soc.value() == 42);
        }
    }

    GIVEN("A bidirectional (BPT) dynamic-mode request") {
        const auto req = d20::ev::state::dc_charge_loop::create_dynamic_request(make_params(), true);
        THEN("It carries the BPT Dynamic DC control mode with discharge limits") {
            REQUIRE(std::holds_alternative<dt::BPT_Dynamic_DC_CLReqControlMode>(req.control_mode));
            const auto& mode = std::get<dt::BPT_Dynamic_DC_CLReqControlMode>(req.control_mode);
            REQUIRE(dt::from_RationalNumber(mode.max_discharge_power) == 11000.0f);
            REQUIRE(dt::from_RationalNumber(mode.min_discharge_power) == 300.0f);
            REQUIRE(dt::from_RationalNumber(mode.max_discharge_current) == 25.0f);
        }
    }

    GIVEN("A scheduled-mode request") {
        const auto req = d20::ev::state::dc_charge_loop::create_scheduled_request(make_params(), false);
        THEN("It carries the Scheduled DC control mode with the target set points") {
            REQUIRE(std::holds_alternative<dt::Scheduled_DC_CLReqControlMode>(req.control_mode));
            const auto& mode = std::get<dt::Scheduled_DC_CLReqControlMode>(req.control_mode);
            REQUIRE(dt::from_RationalNumber(mode.target_current) == 20.0f);
            REQUIRE(dt::from_RationalNumber(mode.target_voltage) == 400.0f);
        }
    }

    GIVEN("A bidirectional (BPT) scheduled-mode request") {
        const auto req = d20::ev::state::dc_charge_loop::create_scheduled_request(make_params(), true);
        THEN("It carries the BPT Scheduled DC control mode with discharge limits") {
            REQUIRE(std::holds_alternative<dt::BPT_Scheduled_DC_CLReqControlMode>(req.control_mode));
            const auto& mode = std::get<dt::BPT_Scheduled_DC_CLReqControlMode>(req.control_mode);
            REQUIRE(mode.max_discharge_power.has_value());
            REQUIRE(dt::from_RationalNumber(mode.max_discharge_power.value()) == 11000.0f);
        }
    }

    GIVEN("A response without a notification") {
        message_20::DC_ChargeLoopResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.present_voltage = dt::from_float(400.0f);
        res.present_current = dt::from_float(125.0f);
        const auto result = d20::ev::state::dc_charge_loop::handle_response(res);
        THEN("It is valid, has no notification and reports present V/A") {
            REQUIRE(result.valid == true);
            REQUIRE(result.notification.has_value() == false);
            REQUIRE(result.present_voltage == 400.0f);
            REQUIRE(result.present_current == 125.0f);
        }
    }

    GIVEN("A response with an EVSE Terminate notification") {
        message_20::DC_ChargeLoopResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.status = dt::EvseStatus{0, dt::EvseNotification::Terminate};
        const auto result = d20::ev::state::dc_charge_loop::handle_response(res);
        THEN("The Terminate notification is reported (stop_from_charger)") {
            REQUIRE(result.notification.has_value());
            REQUIRE(result.notification.value() == dt::EvseNotification::Terminate);
        }
    }

    GIVEN("A response with an EVSE Pause notification") {
        message_20::DC_ChargeLoopResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.status = dt::EvseStatus{60, dt::EvseNotification::Pause};
        const auto result = d20::ev::state::dc_charge_loop::handle_response(res);
        THEN("The Pause notification is reported (pause_from_charger)") {
            REQUIRE(result.notification.has_value());
            REQUIRE(result.notification.value() == dt::EvseNotification::Pause);
        }
    }

    GIVEN("A failed response") {
        message_20::DC_ChargeLoopResponse res;
        res.response_code = dt::ResponseCode::FAILED;
        const auto result = d20::ev::state::dc_charge_loop::handle_response(res);
        THEN("It is invalid") {
            REQUIRE(result.valid == false);
        }
    }
}
