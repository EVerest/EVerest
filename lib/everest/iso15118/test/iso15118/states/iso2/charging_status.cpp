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

    // [V2G2-902]: MeterInfo must accompany a requested MeteringReceipt (PnC), else it is omitted.
    dt::MeterInfo meter{};
    meter.meter_id = "PNX-METER-1";
    meter.meter_reading = 777U;

    GIVEN("A receipt is requested and meter info is available") {
        const auto res = d2::state::handle_request(req, id, config, 1, false, true, meter);
        THEN("ReceiptRequired is true and MeterInfo is present") {
            REQUIRE(res.receipt_required.has_value());
            REQUIRE(res.receipt_required.value() == true);
            REQUIRE(res.meter_info.has_value());
            REQUIRE(res.meter_info->meter_id == "PNX-METER-1");
            REQUIRE(res.meter_info->meter_reading.value_or(0) == 777U);
        }
    }

    GIVEN("No receipt is requested") {
        const auto res = d2::state::handle_request(req, id, config, 1, false, false, meter);
        THEN("MeterInfo is omitted") {
            REQUIRE_FALSE(res.meter_info.has_value());
        }
    }
}
