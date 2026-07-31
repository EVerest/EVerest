// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <variant>

#include <iso15118/detail/d20/ev/state/schedule_exchange.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;

namespace {

d20::ev::state::schedule_exchange::Params make_params() {
    d20::ev::state::schedule_exchange::Params params;
    params.max_supporting_points = 1024;
    params.departure_time = 7200;
    params.minimum_soc = 30;
    params.target_soc = 80;
    params.target_energy = dt::from_float(40000.0f);
    params.max_energy = dt::from_float(60000.0f);
    params.min_energy = dt::from_float(-20000.0f);
    params.max_v2x_energy = dt::from_float(5000.0f);
    params.min_v2x_energy = dt::from_float(0.0f);
    return params;
}

} // namespace

SCENARIO("EVCC ScheduleExchange request/response handling") {
    GIVEN("A dynamic-mode request") {
        const auto req = d20::ev::state::schedule_exchange::create_dynamic_request(make_params());
        THEN("It carries the Dynamic control mode with the configured fields") {
            REQUIRE(req.max_supporting_points == 1024);
            REQUIRE(std::holds_alternative<dt::Dynamic_SEReqControlMode>(req.control_mode));
            const auto& mode = std::get<dt::Dynamic_SEReqControlMode>(req.control_mode);
            REQUIRE(mode.departure_time == 7200);
            REQUIRE(mode.minimum_soc.value() == 30);
            REQUIRE(mode.target_soc.value() == 80);
            REQUIRE(dt::from_RationalNumber(mode.target_energy) == 40000.0f);
            REQUIRE(dt::from_RationalNumber(mode.max_energy) == 60000.0f);
            REQUIRE(dt::from_RationalNumber(mode.min_energy) == -20000.0f);
            REQUIRE(mode.max_v2x_energy.has_value());
            REQUIRE(dt::from_RationalNumber(mode.max_v2x_energy.value()) == 5000.0f);
        }
    }

    GIVEN("A scheduled-mode request") {
        const auto req = d20::ev::state::schedule_exchange::create_scheduled_request(make_params());
        THEN("It carries the Scheduled control mode without an energy offer") {
            REQUIRE(std::holds_alternative<dt::Scheduled_SEReqControlMode>(req.control_mode));
            const auto& mode = std::get<dt::Scheduled_SEReqControlMode>(req.control_mode);
            REQUIRE(mode.departure_time.value() == 7200);
            REQUIRE(mode.energy_offer.has_value() == false);
        }
    }

    GIVEN("A finished response") {
        message_20::ScheduleExchangeResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.processing = dt::Processing::Finished;
        const auto result = d20::ev::state::schedule_exchange::handle_response(res);
        THEN("It is valid and finished (ev_power_ready is published, DC proceeds to CableCheck)") {
            REQUIRE(result.valid == true);
            REQUIRE(result.finished == true);
        }
    }

    GIVEN("An ongoing response") {
        message_20::ScheduleExchangeResponse res;
        res.response_code = dt::ResponseCode::OK;
        res.processing = dt::Processing::Ongoing;
        const auto result = d20::ev::state::schedule_exchange::handle_response(res);
        THEN("It is valid but not finished (resend)") {
            REQUIRE(result.valid == true);
            REQUIRE(result.finished == false);
        }
    }

    GIVEN("A failed response") {
        message_20::ScheduleExchangeResponse res;
        res.response_code = dt::ResponseCode::FAILED;
        const auto result = d20::ev::state::schedule_exchange::handle_response(res);
        THEN("It is invalid") {
            REQUIRE(result.valid == false);
        }
    }
}
