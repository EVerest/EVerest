// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d2/ev/state/charging_status.hpp>
#include <iso15118/message/common_types.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;
namespace charging_status = d2::ev::state::charging_status;

SCENARIO("EVCC ISO-2 ChargingStatus request/response handling") {

    GIVEN("A response with EVSEMaxCurrent and StopCharging notification") {
        message_2::ChargingStatusResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_id = "everest se";
        res.sa_schedule_tuple_id = 2;
        res.evse_max_current = dt::to_physical_value(32.0, dt::Unit::A);
        res.ac_evse_status.notification = dt::EVSENotification::StopCharging;
        const auto result = charging_status::handle_response(res);
        THEN("The notification and max current are surfaced") {
            REQUIRE(result.valid);
            REQUIRE(result.notification.value() == dt::EVSENotification::StopCharging);
            REQUIRE(result.evse_max_current.has_value());
            REQUIRE(result.sa_schedule_tuple_id == 2);
        }
    }

    GIVEN("An EVSE max current of 32 A and a nominal voltage of 230 V") {
        const auto max_current = dt::to_physical_value(32.0, dt::Unit::A);
        const auto nominal_voltage = dt::to_physical_value(230.0, dt::Unit::V);
        const auto target = charging_status::compute_ac_target_power(max_current, nominal_voltage);
        THEN("The target active power is U * I (~7360 W)") {
            REQUIRE(target.target_active_power.has_value());
            const auto power = message_20::datatypes::from_RationalNumber(target.target_active_power.value());
            REQUIRE(power > 7000.0f);
            REQUIRE(power < 7700.0f);
        }
    }

    GIVEN("A response requesting ReNegotiation") {
        message_2::ChargingStatusResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_id = "everest se";
        res.ac_evse_status.notification = dt::EVSENotification::ReNegotiation;
        const auto result = charging_status::handle_response(res);
        THEN("A graceful termination is flagged") {
            REQUIRE(result.valid);
            REQUIRE(result.renegotiation_or_receipt);
        }
    }

    GIVEN("A response with receipt_required set") {
        message_2::ChargingStatusResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_id = "everest se";
        res.receipt_required = true;
        const auto result = charging_status::handle_response(res);
        THEN("A graceful termination is flagged") {
            REQUIRE(result.valid);
            REQUIRE(result.renegotiation_or_receipt);
        }
    }

    GIVEN("No EVSE max current") {
        const auto target = charging_status::compute_ac_target_power(std::nullopt, std::nullopt);
        THEN("The target active power is empty") {
            REQUIRE(not target.target_active_power.has_value());
        }
    }

    GIVEN("A failed response") {
        message_2::ChargingStatusResponse res;
        res.response_code = dt::ResponseCode::FAILED;
        res.evse_id = "everest se";
        const auto result = charging_status::handle_response(res);
        THEN("It is invalid") {
            REQUIRE(not result.valid);
        }
    }
}
