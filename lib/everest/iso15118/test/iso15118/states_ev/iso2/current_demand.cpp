// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d2/ev/state/current_demand.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;
namespace current_demand = d2::ev::state::current_demand;

SCENARIO("EVCC ISO-2 CurrentDemand request/response handling") {

    GIVEN("A request built from parameters") {
        current_demand::RequestParams params;
        params.present_soc = 55;
        params.target_voltage = 400.0f;
        params.target_current = 125.0f;
        params.max_current = 300.0f;
        const auto req = current_demand::create_request(params);
        THEN("Targets, SOC and limits are set, charging not complete") {
            REQUIRE(req.dc_ev_status.ev_ress_soc == 55);
            REQUIRE(dt::from_physical_value(req.ev_target_voltage) == 400.0);
            REQUIRE(dt::from_physical_value(req.ev_target_current) == 125.0);
            REQUIRE(not req.charging_complete);
        }
    }

    GIVEN("A response with StopCharging notification") {
        message_2::CurrentDemandResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_present_voltage = dt::to_physical_value(400.0, dt::Unit::V);
        res.evse_present_current = dt::to_physical_value(120.0, dt::Unit::A);
        res.dc_evse_status.notification = dt::EVSENotification::StopCharging;
        const auto result = current_demand::handle_response(res);
        THEN("The notification is surfaced and a charger stop is requested") {
            REQUIRE(result.valid);
            REQUIRE(result.notification.has_value());
            REQUIRE(result.notification.value() == dt::EVSENotification::StopCharging);
            REQUIRE(result.charger_requested_stop);
            REQUIRE(result.present_current == 120.0f);
        }
    }

    GIVEN("A response with a non-ready DC_EVSEStatus status code") {
        message_2::CurrentDemandResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_present_voltage = dt::to_physical_value(400.0, dt::Unit::V);
        res.evse_present_current = dt::to_physical_value(120.0, dt::Unit::A);
        res.dc_evse_status.notification = dt::EVSENotification::None;
        res.dc_evse_status.status_code = dt::DC_EVSEStatusCode::EVSE_EmergencyShutdown;
        const auto result = current_demand::handle_response(res);
        THEN("A charger stop is requested even without a StopCharging notification") {
            REQUIRE(result.valid);
            REQUIRE(result.charger_requested_stop);
        }
    }

    GIVEN("A response requesting ReNegotiation") {
        message_2::CurrentDemandResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_present_voltage = dt::to_physical_value(400.0, dt::Unit::V);
        res.evse_present_current = dt::to_physical_value(120.0, dt::Unit::A);
        res.dc_evse_status.notification = dt::EVSENotification::ReNegotiation;
        const auto result = current_demand::handle_response(res);
        THEN("A graceful termination is flagged") {
            REQUIRE(result.valid);
            REQUIRE(result.renegotiation_or_receipt);
            REQUIRE(not result.charger_requested_stop);
        }
    }

    GIVEN("A response with receipt_required set") {
        message_2::CurrentDemandResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_present_voltage = dt::to_physical_value(400.0, dt::Unit::V);
        res.evse_present_current = dt::to_physical_value(120.0, dt::Unit::A);
        res.receipt_required = true;
        const auto result = current_demand::handle_response(res);
        THEN("A graceful termination is flagged") {
            REQUIRE(result.valid);
            REQUIRE(result.renegotiation_or_receipt);
        }
    }

    GIVEN("A normal ready response") {
        message_2::CurrentDemandResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_present_voltage = dt::to_physical_value(400.0, dt::Unit::V);
        res.evse_present_current = dt::to_physical_value(120.0, dt::Unit::A);
        const auto result = current_demand::handle_response(res);
        THEN("Neither a stop nor a termination is requested") {
            REQUIRE(result.valid);
            REQUIRE(not result.charger_requested_stop);
            REQUIRE(not result.renegotiation_or_receipt);
        }
    }

    GIVEN("A failed response") {
        message_2::CurrentDemandResponse res;
        res.response_code = dt::ResponseCode::FAILED;
        res.evse_present_voltage = dt::to_physical_value(0.0, dt::Unit::V);
        res.evse_present_current = dt::to_physical_value(0.0, dt::Unit::A);
        const auto result = current_demand::handle_response(res);
        THEN("It is invalid") {
            REQUIRE(not result.valid);
        }
    }
}
