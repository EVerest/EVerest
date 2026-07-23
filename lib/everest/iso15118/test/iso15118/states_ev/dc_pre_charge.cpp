// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d20/ev/state/dc_pre_charge.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;

SCENARIO("EVCC DC_PreCharge request/response handling") {
    GIVEN("An ongoing pre-charge request") {
        const auto req = d20::ev::state::dc_pre_charge::create_request(dt::Processing::Ongoing, dt::from_float(100.0f),
                                                                       dt::from_float(400.0f));
        THEN("The processing, present and target voltage are set") {
            REQUIRE(req.processing == dt::Processing::Ongoing);
            REQUIRE(dt::from_RationalNumber(req.present_voltage) == 100.0f);
            REQUIRE(dt::from_RationalNumber(req.target_voltage) == 400.0f);
        }
    }

    GIVEN("A response with present voltage within +/- 10 % of target (400 V)") {
        message_20::DC_PreChargeResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.present_voltage = dt::from_float(395.0f);
        const auto result = d20::ev::state::dc_pre_charge::handle_response(res, 400.0f);
        THEN("It is converged") {
            REQUIRE(result.valid == true);
            REQUIRE(result.converged == true);
        }
    }

    GIVEN("A response with present voltage just inside the 10 % band (360 V for 400 V target)") {
        message_20::DC_PreChargeResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.present_voltage = dt::from_float(360.0f);
        const auto result = d20::ev::state::dc_pre_charge::handle_response(res, 400.0f);
        THEN("It is converged (exactly at -10 %)") {
            REQUIRE(result.converged == true);
        }
    }

    GIVEN("A response with present voltage outside the 10 % band (300 V for 400 V target)") {
        message_20::DC_PreChargeResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.present_voltage = dt::from_float(300.0f);
        const auto result = d20::ev::state::dc_pre_charge::handle_response(res, 400.0f);
        THEN("It is not converged") {
            REQUIRE(result.valid == true);
            REQUIRE(result.converged == false);
        }
    }

    GIVEN("A failed response") {
        message_20::DC_PreChargeResponse res;
        res.response_code = dt::ResponseCode::FAILED;
        res.present_voltage = dt::from_float(400.0f);
        const auto result = d20::ev::state::dc_pre_charge::handle_response(res, 400.0f);
        THEN("It is invalid and not converged") {
            REQUIRE(result.valid == false);
            REQUIRE(result.converged == false);
        }
    }
}
