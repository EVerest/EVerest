// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/d2/ev/timeouts.hpp>
#include <iso15118/detail/d2/ev/state/welding_detection.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;
namespace welding_detection = d2::ev::state::welding_detection;

SCENARIO("EVCC ISO-2 WeldingDetection request/response handling") {

    GIVEN("A request built from a DC_EVStatus") {
        dt::DC_EVStatus status;
        status.ev_ready = true;
        status.ev_ress_soc = 88;
        const auto req = welding_detection::create_request(status);
        THEN("The EV reports ready with no error and echoes the SoC") {
            REQUIRE(req.dc_ev_status.ev_ready);
            REQUIRE(req.dc_ev_status.ev_error_code == dt::DC_EVErrorCode::NO_ERROR);
            REQUIRE(req.dc_ev_status.ev_ress_soc == 88);
        }
    }

    GIVEN("An OK response") {
        message_2::WeldingDetectionResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_present_voltage = dt::to_physical_value(400.0, dt::Unit::V);
        const auto result = welding_detection::handle_response(res);
        THEN("It is valid and surfaces the present voltage") {
            REQUIRE(result.valid);
            REQUIRE(result.present_voltage > 399.0f);
            REQUIRE(result.present_voltage < 401.0f);
        }
    }

    GIVEN("A failed response") {
        message_2::WeldingDetectionResponse res;
        res.response_code = dt::ResponseCode::FAILED;
        res.evse_present_voltage = dt::to_physical_value(0.0, dt::Unit::V);
        THEN("It is invalid") {
            REQUIRE(not welding_detection::handle_response(res).valid);
        }
    }
}

SCENARIO("EVCC ISO-2 WeldingDetection exit condition") {

    GIVEN("A voltage still above the safe threshold and not enough cycles") {
        THEN("It keeps polling") {
            REQUIRE(not welding_detection::should_finish_welding(1, 400.0f));
        }
    }

    GIVEN("The voltage has dropped below the safe threshold on the first cycle") {
        THEN("It exits early even before the cycle backstop") {
            REQUIRE(welding_detection::should_finish_welding(1, d2::ev::WELDING_DETECTION_SAFE_VOLTAGE_V - 1.0f));
        }
    }

    GIVEN("The voltage is still high but the cycle backstop is reached") {
        THEN("It exits on the cycle budget") {
            REQUIRE(welding_detection::should_finish_welding(d2::ev::WELDING_DETECTION_CYCLES, 400.0f));
        }
    }
}
