// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Per-state response watchdog and Ongoing guard for DIN SPEC 70121: the table itself,
// pinned against the Josev din_spec timeouts it is derived from.

#include <catch2/catch_test_macros.hpp>

#include <chrono>

#include <iso15118/ev/din/states.hpp>
#include <iso15118/ev/din/timeouts.hpp>

using namespace std::chrono_literals;
using iso15118::ev::din::StateID;

namespace timeouts = iso15118::ev::din::timeouts;

SCENARIO("DIN 70121 EV per-message response timeout table") {
    GIVEN("the response timeout table") {
        THEN("CurrentDemand gets the short loop timeout") {
            REQUIRE(timeouts::response_timeout(StateID::CurrentDemand) == 500ms);
        }

        THEN("every other state gets the default message timeout") {
            REQUIRE(timeouts::response_timeout(StateID::SessionSetup) == 2000ms);
            REQUIRE(timeouts::response_timeout(StateID::ChargeParameterDiscovery) == 2000ms);
            REQUIRE(timeouts::response_timeout(StateID::CableCheck) == 2000ms);
            REQUIRE(timeouts::response_timeout(StateID::PreCharge) == 2000ms);
            REQUIRE(timeouts::response_timeout(StateID::SessionStop) == 2000ms);
        }
    }
}

SCENARIO("DIN 70121 EV Ongoing guard table") {
    GIVEN("the ongoing timeout table") {
        THEN("every state that re-polls on Ongoing is bounded") {
            // The defect this guards against is a state that re-sends on EVSEProcessing::Ongoing
            // with no entry here, which holds the EV in that state for as long as the SECC keeps
            // answering. For PreCharge that is an unbounded loop with the DC link ramping.
            for (const auto state : {StateID::ContractAuthentication, StateID::ChargeParameterDiscovery,
                                     StateID::CableCheck, StateID::PreCharge, StateID::WeldingDetection}) {
                REQUIRE(timeouts::ongoing_timeout(state).has_value());
            }
        }

        THEN("the polling states carry their DIN bounds") {
            // Table 77: cable check 40 s, pre-charge 50 s; the rest take
            // V2G_SECC_SEQUENCE_TIMEOUT.
            REQUIRE(timeouts::ongoing_timeout(StateID::ContractAuthentication) == 60000ms);
            REQUIRE(timeouts::ongoing_timeout(StateID::ChargeParameterDiscovery) == 60000ms);
            REQUIRE(timeouts::ongoing_timeout(StateID::CableCheck) == 40000ms);
            REQUIRE(timeouts::ongoing_timeout(StateID::PreCharge) == 50000ms);
            REQUIRE(timeouts::ongoing_timeout(StateID::WeldingDetection) == 60000ms);
        }

        THEN("pre-charge keeps the DIN bound rather than the ISO 15118-2 one") {
            // DIN allows 50 s where -2 allows 7 s relaxed to 10 s. Copying the -2 constant across
            // tightens this 5x and aborts against a slowly ramping supply.
            REQUIRE(timeouts::ONGOING_PRE_CHARGE == 50000ms);
            REQUIRE(timeouts::ONGOING_PRE_CHARGE > 10000ms);
        }

        THEN("states without an Ongoing loop have no bound") {
            REQUIRE_FALSE(timeouts::ongoing_timeout(StateID::SessionSetup).has_value());
            REQUIRE_FALSE(timeouts::ongoing_timeout(StateID::ServiceDiscovery).has_value());
            REQUIRE_FALSE(timeouts::ongoing_timeout(StateID::ServicePaymentSelection).has_value());
            REQUIRE_FALSE(timeouts::ongoing_timeout(StateID::PowerDelivery).has_value());
            REQUIRE_FALSE(timeouts::ongoing_timeout(StateID::CurrentDemand).has_value());
            REQUIRE_FALSE(timeouts::ongoing_timeout(StateID::SessionStop).has_value());
        }
    }
}

SCENARIO("DIN 70121 EV request pacing constant") {
    THEN("the minimum request interval matches EvseV2G MAX_RES_TIME parity") {
        REQUIRE(timeouts::MIN_REQUEST_INTERVAL == 100ms);
    }
}
