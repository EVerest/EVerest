// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d2/state/current_demand.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

SCENARIO("ISO 15118-2 SECC CurrentDemand handling") {
    const dt::SessionId id{};

    d2::SessionConfig config;
    config.evse_id = "DE*PNX*E12345*1";
    config.dc_max_current = 300.0f;
    config.dc_max_power = 150000.0f;
    config.dc_max_voltage = 900.0f;

    message_2::CurrentDemandRequest req;
    req.ev_target_current = dt::to_physical_value(20.0f, dt::Unit::A);
    req.ev_target_voltage = dt::to_physical_value(400.0f, dt::Unit::V);

    GIVEN("A normal charge-loop request") {
        const auto res = d2::state::handle_request(req, id, config, 400.0f, 20.0f, 1, false, false);
        THEN("OK, present values, EVSE id and tuple id echoed, EIM has no receipt") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_id == "DE*PNX*E12345*1");
            REQUIRE(res.sa_schedule_tuple_id == 1);
            REQUIRE(dt::from_physical_value(res.evse_present_voltage) == 400.0);
            REQUIRE(dt::from_physical_value(res.evse_present_current) == 20.0);
            REQUIRE_FALSE(res.evse_current_limit_achieved);
            REQUIRE_FALSE(res.receipt_required.has_value());
            REQUIRE(res.dc_evse_status.notification == dt::EVSENotification::None);
        }
    }

    GIVEN("A charger-initiated stop") {
        const auto res = d2::state::handle_request(req, id, config, 400.0f, 20.0f, 1, true, false);
        THEN("EVSENotification StopCharging and EVSE_Shutdown") {
            REQUIRE(res.dc_evse_status.notification == dt::EVSENotification::StopCharging);
            REQUIRE(res.dc_evse_status.status_code == dt::DC_EVSEStatusCode::EVSE_Shutdown);
        }
    }
}
