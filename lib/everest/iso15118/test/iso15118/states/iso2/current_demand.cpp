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

    GIVEN("A normal charge-loop request") {
        const auto res = d2::state::handle_request(id, config, 400.0f, 20.0f, 1, false, false);
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

    GIVEN("The EVSE delivering at its configured maxima") {
        // [Table 71] the *LimitAchieved flags follow the EVSE's present output, not the EV's target.
        const auto res = d2::state::handle_request(id, config, 900.0f, 300.0f, 1, false, false);
        THEN("all three limit-achieved flags are set") {
            REQUIRE(res.evse_current_limit_achieved);
            REQUIRE(res.evse_voltage_limit_achieved);
            REQUIRE(res.evse_power_limit_achieved);
        }
    }

    GIVEN("The EVSE delivering well below its maxima") {
        // Whatever the EV asks for, no limit is achieved while the EVSE output stays below its maxima.
        const auto res = d2::state::handle_request(id, config, 400.0f, 20.0f, 1, false, false);
        THEN("no limit is reported as achieved") {
            REQUIRE_FALSE(res.evse_current_limit_achieved);
            REQUIRE_FALSE(res.evse_voltage_limit_achieved);
            REQUIRE_FALSE(res.evse_power_limit_achieved);
        }
    }

    GIVEN("A charger-initiated stop") {
        const auto res = d2::state::handle_request(id, config, 400.0f, 20.0f, 1, true, false);
        THEN("EVSENotification StopCharging and EVSE_Shutdown") {
            REQUIRE(res.dc_evse_status.notification == dt::EVSENotification::StopCharging);
            REQUIRE(res.dc_evse_status.status_code == dt::DC_EVSEStatusCode::EVSE_Shutdown);
        }
    }

    // [V2G2-902]: when a MeteringReceipt is requested (PnC) the response must carry the MeterInfo the EV
    // will sign. MeterInfo itself is a PnC-only element of CurrentDemandRes ([Table 104]: "-" for both
    // peers in the Message Set "DC Charging EIM", [V2G2-666]).
    dt::MeterInfo meter{};
    meter.meter_id = "PNX-METER-1";
    meter.meter_reading = 4211U;

    GIVEN("A receipt is requested and meter info is available") {
        const auto res = d2::state::handle_request(id, config, 400.0f, 20.0f, 1, false, true, meter, true);
        THEN("ReceiptRequired is true and MeterInfo is present with the forwarded reading") {
            REQUIRE(res.receipt_required.has_value());
            REQUIRE(res.receipt_required.value() == true);
            REQUIRE(res.meter_info.has_value());
            REQUIRE(res.meter_info->meter_id == "PNX-METER-1");
            REQUIRE(res.meter_info->meter_reading.value_or(0) == 4211U);
        }
    }

    GIVEN("A PnC session with a meter reading but no receipt request") {
        const auto res = d2::state::handle_request(id, config, 400.0f, 20.0f, 1, false, false, meter, true);
        THEN("MeterInfo is still reported") {
            REQUIRE_FALSE(res.receipt_required.has_value());
            REQUIRE(res.meter_info.has_value());
            REQUIRE(res.meter_info->meter_id == "PNX-METER-1");
            REQUIRE(res.meter_info->meter_reading.value_or(0) == 4211U);
        }
    }

    GIVEN("An EIM session with a meter reading") {
        const auto res = d2::state::handle_request(id, config, 400.0f, 20.0f, 1, false, false, meter, false);
        THEN("MeterInfo is omitted, the element does not exist in the DC EIM Message Set") {
            REQUIRE_FALSE(res.meter_info.has_value());
        }
    }
}
