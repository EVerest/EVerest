// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d20/ev/state/dc_welding_detection.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;

SCENARIO("EVCC DC_WeldingDetection request/response handling") {
    GIVEN("An ongoing welding-detection request") {
        const auto req = d20::ev::state::dc_welding_detection::create_request(dt::Processing::Ongoing);
        THEN("processing is Ongoing") {
            REQUIRE(req.processing == dt::Processing::Ongoing);
        }
    }

    GIVEN("A finished welding-detection request") {
        const auto req = d20::ev::state::dc_welding_detection::create_request(dt::Processing::Finished);
        THEN("processing is Finished") {
            REQUIRE(req.processing == dt::Processing::Finished);
        }
    }

    GIVEN("An OK response") {
        message_20::DC_WeldingDetectionResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.present_voltage = dt::from_float(400.0f);
        const auto result = d20::ev::state::dc_welding_detection::handle_response(res);
        THEN("It is valid") {
            REQUIRE(result.valid == true);
        }
    }

    GIVEN("A failed response") {
        message_20::DC_WeldingDetectionResponse res;
        res.response_code = dt::ResponseCode::FAILED;
        const auto result = d20::ev::state::dc_welding_detection::handle_response(res);
        THEN("It is invalid") {
            REQUIRE(result.valid == false);
        }
    }
}
