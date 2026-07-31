// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d2/ev/state/pre_charge.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;
namespace pre_charge = d2::ev::state::pre_charge;

namespace {
message_2::PreChargeResponse make_response(float present_voltage) {
    message_2::PreChargeResponse res;
    res.response_code = dt::ResponseCode::OK;
    res.evse_present_voltage = dt::to_physical_value(present_voltage, dt::Unit::V);
    return res;
}
} // namespace

SCENARIO("EVCC ISO-2 PreCharge request/response handling") {

    GIVEN("A request built from a DC_EVStatus") {
        dt::DC_EVStatus status;
        status.ev_ready = true;
        status.ev_ress_soc = 30;
        const auto req = pre_charge::create_request(status, 100.0f, 400.0f);
        THEN("Target voltage is set, target current is zero and the SoC is echoed") {
            REQUIRE(dt::from_physical_value(req.ev_target_voltage) == 400.0);
            REQUIRE(dt::from_physical_value(req.ev_target_current) == 0.0);
            REQUIRE(req.dc_ev_status.ev_ress_soc == 30);
        }
    }

    GIVEN("Present voltage well within the band and absolute cap (395 V for 400 V)") {
        const auto result = pre_charge::handle_response(make_response(395.0f), 400.0f);
        THEN("It is converged") {
            REQUIRE(result.valid);
            REQUIRE(result.converged);
        }
    }

    GIVEN("Present voltage exactly at the -10 % boundary (360 V for 400 V)") {
        const auto result = pre_charge::handle_response(make_response(360.0f), 400.0f);
        THEN("It is not converged (band is exclusive)") {
            REQUIRE(result.valid);
            REQUIRE(not result.converged);
        }
    }

    GIVEN("Present voltage inside the band but outside the 20 V absolute cap (361 V for 400 V)") {
        const auto result = pre_charge::handle_response(make_response(361.0f), 400.0f);
        THEN("It is not converged because it exceeds the absolute cap") {
            REQUIRE(result.valid);
            REQUIRE(not result.converged);
        }
    }

    GIVEN("Present voltage exactly at the 20 V absolute cap (380 V for 400 V)") {
        const auto result = pre_charge::handle_response(make_response(380.0f), 400.0f);
        THEN("It is converged") {
            REQUIRE(result.converged);
        }
    }

    GIVEN("Present voltage far below target (300 V for 400 V)") {
        const auto result = pre_charge::handle_response(make_response(300.0f), 400.0f);
        THEN("It is not converged") {
            REQUIRE(result.valid);
            REQUIRE(not result.converged);
        }
    }

    GIVEN("A high target where the band alone would admit a large error (700 V for 750 V)") {
        // Within +/- 10 % (675..825) but 50 V away, so the absolute cap must reject it.
        const auto result = pre_charge::handle_response(make_response(700.0f), 750.0f);
        THEN("The absolute cap keeps it from converging") {
            REQUIRE(not result.converged);
        }
    }

    GIVEN("A failed response") {
        auto res = make_response(400.0f);
        res.response_code = dt::ResponseCode::FAILED;
        const auto result = pre_charge::handle_response(res, 400.0f);
        THEN("It is invalid and not converged") {
            REQUIRE(not result.valid);
            REQUIRE(not result.converged);
        }
    }
}
