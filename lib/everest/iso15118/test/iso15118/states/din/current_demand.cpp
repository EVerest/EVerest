// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/din/state/current_demand.hpp>

using namespace iso15118;
namespace dt = message_din::datatypes;

SCENARIO("DIN SECC CurrentDemand state handling") {
    const dt::SessionId session{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    din::SessionConfig config;
    config.evse_maximum_current_limit = 400.0;
    config.evse_maximum_power_limit = 360000.0;
    config.evse_maximum_voltage_limit = 920.0;

    GIVEN("A current demand request") {
        message_din::CurrentDemandRequest req;
        req.header.session_id = session;

        const auto res = din::state::handle_request(req, config, 400.0f, 125.0f, session);
        THEN("The present voltage/current and limits are reported") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.evse_present_voltage == 400.0);
            REQUIRE(res.evse_present_current == 125.0);
            REQUIRE(res.evse_maximum_current_limit == 400.0);
            REQUIRE(res.dc_evse_status.evse_status_code == dt::DcEvseStatusCode::EVSE_Ready);
            REQUIRE(res.evse_current_limit_achieved == false);
        }
    }

    GIVEN("The EVSE delivering at its configured maxima") {
        message_din::CurrentDemandRequest req;
        req.header.session_id = session;

        // The *LimitAchieved flags follow the EVSE's present output against the advertised maxima.
        const auto res = din::state::handle_request(req, config, 920.0f, 400.0f, session);
        THEN("all three limit-achieved flags are set") {
            REQUIRE(res.evse_current_limit_achieved);
            REQUIRE(res.evse_voltage_limit_achieved);
            REQUIRE(res.evse_power_limit_achieved);
        }
    }

    GIVEN("An EVSE with no EVSEMaximumPowerLimit advertised") {
        message_din::CurrentDemandRequest req;
        req.header.session_id = session;

        auto no_power_limit = config;
        no_power_limit.evse_maximum_power_limit.reset();

        const auto res = din::state::handle_request(req, no_power_limit, 920.0f, 400.0f, session);
        THEN("the power limit can never be achieved, the other two still are") {
            REQUIRE(res.evse_current_limit_achieved);
            REQUIRE(res.evse_voltage_limit_achieved);
            REQUIRE_FALSE(res.evse_power_limit_achieved);
        }
    }

    GIVEN("A request carrying only the EV voltage and current maxima") {
        message_din::CurrentDemandRequest req;
        req.header.session_id = session;
        req.ev_target_voltage = 400.0;
        req.ev_target_current = 100.0;
        req.ev_maximum_voltage_limit = 920.0;
        req.ev_maximum_current_limit = 200.0;
        // EVMaximumPowerLimit omitted -- optional on its own in the DIN schema.

        const auto mode = din::state::build_ev_setpoint(req);
        THEN("the two limits the EV sent are forwarded, the omitted one is absent") {
            REQUIRE(message_20::datatypes::from_RationalNumber(mode.target_voltage) == 400.0f);
            REQUIRE(message_20::datatypes::from_RationalNumber(mode.target_current) == 100.0f);
            REQUIRE(mode.max_voltage.has_value());
            REQUIRE(message_20::datatypes::from_RationalNumber(mode.max_voltage.value()) == 920.0f);
            REQUIRE(mode.max_charge_current.has_value());
            REQUIRE(message_20::datatypes::from_RationalNumber(mode.max_charge_current.value()) == 200.0f);
            REQUIRE(mode.max_charge_power.has_value() == false);
        }
    }

    GIVEN("A charger-initiated stop") {
        message_din::CurrentDemandRequest req;
        req.header.session_id = session;

        const auto res = din::state::handle_request(req, config, 400.0f, 125.0f, session, true);
        THEN("EVSE_Shutdown is signalled via the status code; EVSENotification stays None [V2G-DC-500]") {
            REQUIRE(res.dc_evse_status.evse_status_code == dt::DcEvseStatusCode::EVSE_Shutdown);
            REQUIRE(res.dc_evse_status.evse_notification == dt::EvseNotification::None);
        }
    }

    GIVEN("A module-reported EVSE error (Malfunction)") {
        message_din::CurrentDemandRequest req;
        req.header.session_id = session;
        // An active send_error status override wins over the normal EVSE_Ready code (EvseV2G parity).
        const auto res = din::state::handle_request(req, config, 400.0f, 125.0f, session, /*charger_stop=*/false,
                                                    dt::DcEvseStatusCode::EVSE_Malfunction);
        THEN("the status code is EVSE_Malfunction") {
            REQUIRE(res.dc_evse_status.evse_status_code == dt::DcEvseStatusCode::EVSE_Malfunction);
        }
    }
}
