// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <variant>

#include <iso15118/detail/d20/ev/state/ac_charge_loop.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;

namespace {

d20::ev::state::ac_charge_loop::RequestParams make_params() {
    d20::ev::state::ac_charge_loop::RequestParams p;
    p.departure_time = 7200;
    p.display.present_soc = 42;
    p.target_energy_request = dt::from_float(40000.0f);
    p.max_energy_request = dt::from_float(60000.0f);
    p.min_energy_request = dt::from_float(-20000.0f);
    p.max_charge_power = dt::from_float(15000.0f);
    p.min_charge_power = dt::from_float(100.0f);
    p.present_active_power = dt::from_float(15000.0f);
    p.present_reactive_power = dt::from_float(0.0f);
    p.max_discharge_power = dt::from_float(11000.0f);
    p.min_discharge_power = dt::from_float(1.0f);
    return p;
}

} // namespace

SCENARIO("EVCC AC_ChargeLoop request/response handling") {
    GIVEN("A dynamic-mode request") {
        const auto req = d20::ev::state::ac_charge_loop::create_dynamic_request(make_params(), false);
        THEN("It carries the Dynamic AC control mode with the energy window and present powers") {
            REQUIRE(std::holds_alternative<dt::Dynamic_AC_CLReqControlMode>(req.control_mode));
            const auto& mode = std::get<dt::Dynamic_AC_CLReqControlMode>(req.control_mode);
            REQUIRE(mode.departure_time.has_value());
            REQUIRE(mode.departure_time.value() == 7200);
            REQUIRE(dt::from_RationalNumber(mode.target_energy_request) == 40000.0f);
            REQUIRE(dt::from_RationalNumber(mode.max_charge_power) == 15000.0f);
            REQUIRE(dt::from_RationalNumber(mode.min_charge_power) == 100.0f);
            REQUIRE(dt::from_RationalNumber(mode.present_active_power) == 15000.0f);
            REQUIRE(req.meter_info_requested == false);
            REQUIRE(req.display_parameters.has_value());
            REQUIRE(req.display_parameters->present_soc.value() == 42);
        }
    }

    GIVEN("A bidirectional (BPT) dynamic-mode request") {
        const auto req = d20::ev::state::ac_charge_loop::create_dynamic_request(make_params(), true);
        THEN("It carries the BPT Dynamic AC control mode with discharge limits") {
            REQUIRE(std::holds_alternative<dt::BPT_Dynamic_AC_CLReqControlMode>(req.control_mode));
            const auto& mode = std::get<dt::BPT_Dynamic_AC_CLReqControlMode>(req.control_mode);
            REQUIRE(dt::from_RationalNumber(mode.max_discharge_power) == 11000.0f);
            REQUIRE(dt::from_RationalNumber(mode.min_discharge_power) == 1.0f);
        }
    }

    GIVEN("A scheduled-mode request") {
        const auto req = d20::ev::state::ac_charge_loop::create_scheduled_request(make_params(), false);
        THEN("It carries the Scheduled AC control mode with the present active power") {
            REQUIRE(std::holds_alternative<dt::Scheduled_AC_CLReqControlMode>(req.control_mode));
            const auto& mode = std::get<dt::Scheduled_AC_CLReqControlMode>(req.control_mode);
            REQUIRE(dt::from_RationalNumber(mode.present_active_power) == 15000.0f);
            REQUIRE(mode.max_charge_power.has_value());
            REQUIRE(dt::from_RationalNumber(mode.max_charge_power.value()) == 15000.0f);
        }
    }

    GIVEN("A bidirectional (BPT) scheduled-mode request") {
        const auto req = d20::ev::state::ac_charge_loop::create_scheduled_request(make_params(), true);
        THEN("It carries the BPT Scheduled AC control mode with discharge limits") {
            REQUIRE(std::holds_alternative<dt::BPT_Scheduled_AC_CLReqControlMode>(req.control_mode));
            const auto& mode = std::get<dt::BPT_Scheduled_AC_CLReqControlMode>(req.control_mode);
            REQUIRE(mode.max_discharge_power.has_value());
            REQUIRE(dt::from_RationalNumber(mode.max_discharge_power.value()) == 11000.0f);
        }
    }

    GIVEN("A dynamic response with a target active power") {
        message_20::AC_ChargeLoopResponse res;
        res.response_code = dt::ResponseCode::OK;
        auto& mode = res.control_mode.emplace<dt::Dynamic_AC_CLResControlMode>();
        mode.target_active_power = dt::from_float(7000.0f);
        mode.target_reactive_power = dt::from_float(500.0f);
        res.target_frequency = dt::from_float(50.0f);

        const auto result = d20::ev::state::ac_charge_loop::handle_response(res);
        THEN("It is valid, has no notification and reports the target power (fed via ac_evse_target_power)") {
            REQUIRE(result.valid == true);
            REQUIRE(result.notification.has_value() == false);
            REQUIRE(result.target.target_active_power.has_value());
            REQUIRE(dt::from_RationalNumber(result.target.target_active_power.value()) == 7000.0f);
            REQUIRE(result.target.target_reactive_power.has_value());
            REQUIRE(dt::from_RationalNumber(result.target.target_reactive_power.value()) == 500.0f);
            REQUIRE(result.target.target_frequency.has_value());
            REQUIRE(dt::from_RationalNumber(result.target.target_frequency.value()) == 50.0f);
        }
    }

    GIVEN("A scheduled response without a target active power") {
        message_20::AC_ChargeLoopResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.control_mode.emplace<dt::Scheduled_AC_CLResControlMode>();

        const auto result = d20::ev::state::ac_charge_loop::handle_response(res);
        THEN("It is valid and the target active power is absent") {
            REQUIRE(result.valid == true);
            REQUIRE(result.target.target_active_power.has_value() == false);
        }
    }

    GIVEN("A response with an EVSE Terminate notification") {
        message_20::AC_ChargeLoopResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.control_mode.emplace<dt::Dynamic_AC_CLResControlMode>();
        res.status = dt::EvseStatus{0, dt::EvseNotification::Terminate};
        const auto result = d20::ev::state::ac_charge_loop::handle_response(res);
        THEN("The Terminate notification is reported (stop_from_charger)") {
            REQUIRE(result.notification.has_value());
            REQUIRE(result.notification.value() == dt::EvseNotification::Terminate);
        }
    }

    GIVEN("A response with an EVSE Pause notification") {
        message_20::AC_ChargeLoopResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.control_mode.emplace<dt::Dynamic_AC_CLResControlMode>();
        res.status = dt::EvseStatus{60, dt::EvseNotification::Pause};
        const auto result = d20::ev::state::ac_charge_loop::handle_response(res);
        THEN("The Pause notification is reported (pause_from_charger)") {
            REQUIRE(result.notification.has_value());
            REQUIRE(result.notification.value() == dt::EvseNotification::Pause);
        }
    }

    GIVEN("A failed response") {
        message_20::AC_ChargeLoopResponse res;
        res.response_code = dt::ResponseCode::FAILED;
        const auto result = d20::ev::state::ac_charge_loop::handle_response(res);
        THEN("It is invalid") {
            REQUIRE(result.valid == false);
        }
    }
}
