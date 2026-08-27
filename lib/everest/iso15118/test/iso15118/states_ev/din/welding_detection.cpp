// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/din/ev/state/welding_detection.hpp>

using namespace iso15118;
namespace dt = message_din::datatypes;
using namespace din::ev::state::welding_detection;

SCENARIO("EVCC DIN WeldingDetection request/response handling") {
    GIVEN("A request") {
        dt::DcEvStatus status;
        status.ev_ress_soc = 80;
        const auto req = create_request(status);
        THEN("The DC EV status is carried") {
            REQUIRE(req.dc_ev_status.ev_ress_soc == 80);
        }
    }

    GIVEN("An OK response") {
        message_din::WeldingDetectionResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.evse_present_voltage = 400.0;
        THEN("It is valid and the present voltage is captured") {
            const auto result = handle_response(res);
            REQUIRE(result.valid);
            REQUIRE(result.present_voltage == 400.0f);
        }
    }

    GIVEN("A failed response") {
        message_din::WeldingDetectionResponse res;
        res.response_code = dt::ResponseCode::FAILED;
        THEN("It is invalid") {
            REQUIRE(not handle_response(res).valid);
        }
    }
}
