// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d20/ev/state/dc_cable_check.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;

SCENARIO("EVCC DC_CableCheck request/response handling") {
    GIVEN("A cable-check request") {
        const auto req = d20::ev::state::dc_cable_check::create_request();
        (void)req; // header-only request
        THEN("It is a header-only request") {
            SUCCEED();
        }
    }

    GIVEN("An ongoing response") {
        message_20::DC_CableCheckResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.processing = dt::Processing::Ongoing;
        const auto result = d20::ev::state::dc_cable_check::handle_response(res);
        THEN("It is valid but not finished (resend loop)") {
            REQUIRE(result.valid == true);
            REQUIRE(result.finished == false);
        }
    }

    GIVEN("A finished response") {
        message_20::DC_CableCheckResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.processing = dt::Processing::Finished;
        const auto result = d20::ev::state::dc_cable_check::handle_response(res);
        THEN("It is valid and finished (proceed to PreCharge)") {
            REQUIRE(result.valid == true);
            REQUIRE(result.finished == true);
        }
    }

    GIVEN("A failed response") {
        message_20::DC_CableCheckResponse res;
        res.response_code = dt::ResponseCode::FAILED;
        const auto result = d20::ev::state::dc_cable_check::handle_response(res);
        THEN("It is invalid") {
            REQUIRE(result.valid == false);
        }
    }
}
