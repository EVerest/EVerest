// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <optional>

#include <iso15118/detail/d20/ev/state/power_delivery.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;

SCENARIO("EVCC PowerDelivery request/response handling") {
    GIVEN("A Start request (no BPT channel)") {
        const auto req = d20::ev::state::power_delivery::create_request(dt::Progress::Start, std::nullopt);
        THEN("charge_progress is Start, processing Finished and no channel selection") {
            REQUIRE(req.charge_progress == dt::Progress::Start);
            REQUIRE(req.processing == dt::Processing::Finished);
            REQUIRE(req.channel_selection.has_value() == false);
        }
    }

    GIVEN("A Stop request") {
        const auto req = d20::ev::state::power_delivery::create_request(dt::Progress::Stop, std::nullopt);
        THEN("charge_progress is Stop and never Standby") {
            REQUIRE(req.charge_progress == dt::Progress::Stop);
            REQUIRE(req.charge_progress != dt::Progress::Standby);
        }
    }

    GIVEN("A BPT Start request with Charge channel selection") {
        const auto req =
            d20::ev::state::power_delivery::create_request(dt::Progress::Start, dt::ChannelSelection::Charge);
        THEN("The channel selection is Charge") {
            REQUIRE(req.channel_selection.has_value());
            REQUIRE(req.channel_selection.value() == dt::ChannelSelection::Charge);
        }
    }

    GIVEN("An OK response") {
        message_20::PowerDeliveryResponse res;
        res.response_code = dt::ResponseCode::OK;
        const auto result = d20::ev::state::power_delivery::handle_response(res);
        THEN("It is valid") {
            REQUIRE(result.valid == true);
        }
    }

    GIVEN("A FAILED_PowerDeliveryNotApplied response") {
        message_20::PowerDeliveryResponse res;
        res.response_code = dt::ResponseCode::FAILED_PowerDeliveryNotApplied;
        const auto result = d20::ev::state::power_delivery::handle_response(res);
        THEN("It is invalid") {
            REQUIRE(result.valid == false);
        }
    }
}
