// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/din/ev/state/current_demand.hpp>

using namespace iso15118;
namespace dt = message_din::datatypes;
using namespace din::ev::state::current_demand;

SCENARIO("EVCC DIN CurrentDemand request/response handling") {
    GIVEN("A request built from params") {
        RequestParams p;
        p.dc_ev_status.ev_ready = true;
        p.target_voltage = 400.0;
        p.target_current = 125.0;
        p.max_voltage_limit = 900.0;
        p.max_current_limit = 300.0;
        p.max_power_limit = 150000.0;
        p.charging_complete = false;
        const auto req = create_request(p);
        THEN("The target and limit fields are populated") {
            REQUIRE(req.ev_target_voltage == 400.0);
            REQUIRE(req.ev_target_current == 125.0);
            REQUIRE(req.ev_maximum_voltage_limit == 900.0);
            REQUIRE(req.ev_maximum_current_limit == 300.0);
            REQUIRE(req.ev_maximum_power_limit == 150000.0);
            REQUIRE(not req.charging_complete);
        }
    }

    GIVEN("A response with the EVSE ready") {
        message_din::CurrentDemandResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
        res.dc_evse_status.evse_notification = dt::EvseNotification::None;
        res.evse_present_voltage = 400.0;
        res.evse_present_current = 125.0;
        const auto result = handle_response(res);
        THEN("Charging continues") {
            REQUIRE(result.valid);
            REQUIRE(result.charger_state == ChargerState::Continue);
            REQUIRE(result.present_voltage == 400.0f);
            REQUIRE(result.present_current == 125.0f);
        }
    }

    GIVEN("A response with EVSENotification StopCharging") {
        message_din::CurrentDemandResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
        res.dc_evse_status.evse_notification = dt::EvseNotification::StopCharging;
        const auto result = handle_response(res);
        THEN("The charger requests a stop") {
            REQUIRE(result.charger_state == ChargerState::Stop);
        }
    }

    GIVEN("A response with the EVSE not ready") {
        message_din::CurrentDemandResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Shutdown;
        const auto result = handle_response(res);
        THEN("The charger requests a stop") {
            REQUIRE(result.charger_state == ChargerState::Stop);
        }
    }

    GIVEN("A response with the EVSE in emergency shutdown") {
        message_din::CurrentDemandResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_EmergencyShutdown;
        const auto result = handle_response(res);
        THEN("The charger requests a stop") {
            REQUIRE(result.valid);
            REQUIRE(result.charger_state == ChargerState::Stop);
        }
    }

    GIVEN("A response with the EVSE malfunctioning") {
        message_din::CurrentDemandResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Malfunction;
        const auto result = handle_response(res);
        THEN("Charging continues (informational only, [V2G-DC-637])") {
            REQUIRE(result.valid);
            REQUIRE(result.charger_state == ChargerState::Continue);
        }
    }

    GIVEN("A response with isolation monitoring still active") {
        message_din::CurrentDemandResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_IsolationMonitoringActive;
        const auto result = handle_response(res);
        THEN("Charging continues (informational only, [V2G-DC-637])") {
            REQUIRE(result.valid);
            REQUIRE(result.charger_state == ChargerState::Continue);
        }
    }

    GIVEN("A response with the EVSE not ready") {
        message_din::CurrentDemandResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_NotReady;
        const auto result = handle_response(res);
        THEN("Charging continues (informational only, [V2G-DC-637])") {
            REQUIRE(result.valid);
            REQUIRE(result.charger_state == ChargerState::Continue);
        }
    }

    GIVEN("A failed response") {
        message_din::CurrentDemandResponse res;
        res.response_code = dt::ResponseCode::FAILED;
        THEN("It is invalid") {
            REQUIRE(not handle_response(res).valid);
        }
    }
}
