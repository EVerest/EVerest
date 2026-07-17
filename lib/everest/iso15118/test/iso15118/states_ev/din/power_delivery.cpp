// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/din/ev/state/power_delivery.hpp>

using namespace iso15118;
namespace dt = message_din::datatypes;
using namespace din::ev::state::power_delivery;

SCENARIO("EVCC DIN PowerDelivery request/response handling") {
    dt::DcEvStatus status;
    status.ev_ready = true;

    GIVEN("A start request") {
        const auto req = create_request(true, status, false);
        THEN("ReadyToChargeState is true and charging is not complete") {
            REQUIRE(req.ready_to_charge_state);
            REQUIRE(req.dc_ev_power_delivery_parameter.has_value());
            REQUIRE(not req.dc_ev_power_delivery_parameter->charging_complete);
        }
    }

    GIVEN("A stop request") {
        const auto req = create_request(false, status, true);
        THEN("ReadyToChargeState is false and charging is complete") {
            REQUIRE(not req.ready_to_charge_state);
            REQUIRE(req.dc_ev_power_delivery_parameter->charging_complete);
        }
    }

    GIVEN("An OK response") {
        message_din::PowerDeliveryResponse res;
        res.response_code = dt::ResponseCode::OK;
        THEN("It is valid") {
            REQUIRE(handle_response(res).valid);
        }
    }

    GIVEN("A failed response") {
        message_din::PowerDeliveryResponse res;
        res.response_code = dt::ResponseCode::FAILED_PowerDeliveryNotApplied;
        THEN("It is invalid") {
            REQUIRE(not handle_response(res).valid);
        }
    }
}
