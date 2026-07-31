// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/din/ev/state/pre_charge.hpp>

using namespace iso15118;
namespace dt = message_din::datatypes;
using namespace din::ev::state::pre_charge;

SCENARIO("EVCC DIN PreCharge request/response handling") {
    GIVEN("A request") {
        dt::DcEvStatus status;
        status.ev_ready = true;
        const auto req = create_request(status, 400.0, 0.0);
        THEN("Target voltage/current and status are set (0 A follow Josev)") {
            REQUIRE(req.ev_target_voltage == 400.0);
            REQUIRE(req.ev_target_current == 0.0);
            REQUIRE(req.dc_ev_status.ev_ready);
        }
    }

    GIVEN("A response within +/- 10 % of a 400 V target (395 V)") {
        message_din::PreChargeResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_present_voltage = 395.0;
        const auto result = handle_response(res, 400.0);
        THEN("It is converged") {
            REQUIRE(result.valid);
            REQUIRE(result.converged);
        }
    }

    GIVEN("A response exactly at the +20 V absolute cap (420 V for 400 V)") {
        message_din::PreChargeResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_present_voltage = 420.0;
        THEN("It is converged (inclusive absolute cap)") {
            REQUIRE(handle_response(res, 400.0).converged);
        }
    }

    GIVEN("A response within the 10 % band but beyond the 20 V absolute cap (425 V for 400 V)") {
        message_din::PreChargeResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_present_voltage = 425.0;
        THEN("It is not converged (absolute cap dominates)") {
            REQUIRE(not handle_response(res, 400.0).converged);
        }
    }

    GIVEN("A response outside the 10 % band (300 V for 400 V)") {
        message_din::PreChargeResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_present_voltage = 300.0;
        THEN("It is not converged") {
            REQUIRE(not handle_response(res, 400.0).converged);
        }
    }

    GIVEN("A failed response") {
        message_din::PreChargeResponse res;
        res.response_code = dt::ResponseCode::FAILED_EVSEPresentVoltageToLow;
        res.evse_present_voltage = 400.0;
        const auto result = handle_response(res, 400.0);
        THEN("It is invalid and not converged") {
            REQUIRE(not result.valid);
            REQUIRE(not result.converged);
        }
    }
}
