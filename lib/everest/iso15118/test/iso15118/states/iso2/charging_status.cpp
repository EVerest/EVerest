// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d2/state/charging_status.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

SCENARIO("ISO 15118-2 SECC ChargingStatus handling") {
    const dt::SessionId id{};

    d2::SessionConfig config;
    config.evse_id = "DE*PNX*E12345*1";
    config.ac_max_current = 32.0f;

    message_2::ChargingStatusRequest req;

    GIVEN("A normal AC charge-loop request") {
        const auto res = d2::state::handle_request(req, id, config, 1, false, false);
        THEN("OK, EVSE id and tuple id echoed, receipt present but false for EIM") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_id == "DE*PNX*E12345*1");
            REQUIRE(res.sa_schedule_tuple_id == 1);
            REQUIRE(res.evse_max_current.has_value());
            REQUIRE(res.receipt_required.has_value());
            REQUIRE(res.receipt_required.value() == false);
            REQUIRE(res.ac_evse_status.notification == dt::EVSENotification::None);
        }
    }

    GIVEN("A charger-initiated stop") {
        const auto res = d2::state::handle_request(req, id, config, 1, true, false);
        THEN("EVSENotification StopCharging") {
            REQUIRE(res.ac_evse_status.notification == dt::EVSENotification::StopCharging);
        }
    }
}
