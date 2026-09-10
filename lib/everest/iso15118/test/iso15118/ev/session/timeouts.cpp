// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Per-state response watchdog and Ongoing guard: the table itself, plus one reactor
// run proving a zero-configured response_timeout falls back to it.

#include <catch2/catch_test_macros.hpp>

#include <chrono>

#include <iso15118/ev/d20/timeouts.hpp>

#include "test_support.hpp"

using namespace iso15118;
using namespace std::chrono_literals;
using namespace iso15118::ev::test;
using iso15118::ev::d20::StateID;

namespace timeouts = iso15118::ev::d20::timeouts;

SCENARIO("ISO15118-20 EV per-message response timeout table") {
    GIVEN("the response timeout table") {
        THEN("ServiceDetail gets the longer V2G_EVCC_Msg_Timeout") {
            REQUIRE(timeouts::response_timeout(StateID::ServiceDetail) == 5000ms);
        }

        THEN("every charge loop gets the short loop timeout") {
            REQUIRE(timeouts::response_timeout(StateID::AC_ChargeLoop) == 500ms);
            REQUIRE(timeouts::response_timeout(StateID::AC_DER_IEC_ChargeLoop) == 500ms);
            REQUIRE(timeouts::response_timeout(StateID::DC_ChargeLoop) == 500ms);
        }

        THEN("every other state gets the default message timeout") {
            REQUIRE(timeouts::response_timeout(StateID::SupportedAppProtocol) == 2000ms);
            REQUIRE(timeouts::response_timeout(StateID::SessionSetup) == 2000ms);
            REQUIRE(timeouts::response_timeout(StateID::DC_CableCheck) == 2000ms);
            REQUIRE(timeouts::response_timeout(StateID::PowerDelivery) == 2000ms);
            REQUIRE(timeouts::response_timeout(StateID::SessionStop) == 2000ms);
        }
    }
}

SCENARIO("ISO15118-20 EV Ongoing guard table") {
    GIVEN("the ongoing timeout table") {
        THEN("the polling states carry their standard bounds") {
            REQUIRE(timeouts::ongoing_timeout(StateID::Authorization) == 60000ms);
            REQUIRE(timeouts::ongoing_timeout(StateID::DC_CableCheck) == 40000ms);
            REQUIRE(timeouts::ongoing_timeout(StateID::DC_PreCharge) == 10000ms);
            REQUIRE(timeouts::ongoing_timeout(StateID::DC_WeldingDetection) == 60000ms);
        }

        THEN("states without an Ongoing loop have no bound") {
            REQUIRE_FALSE(timeouts::ongoing_timeout(StateID::SessionSetup).has_value());
            REQUIRE_FALSE(timeouts::ongoing_timeout(StateID::DC_ChargeLoop).has_value());
            REQUIRE_FALSE(timeouts::ongoing_timeout(StateID::SessionStop).has_value());
        }
    }
}

SCENARIO("ISO15118-20 EV pre-session timing constants") {
    THEN("the SDP discovery bounds match the standard") {
        REQUIRE(timeouts::SDP_MAX_REQUESTS == 50);
        REQUIRE(timeouts::SDP_RESEND_INTERVAL == 250ms);
    }
}

SCENARIO("ISO15118-20 EV Session falls back to the table when response_timeout is zero") {
    GIVEN("a Session configured with response_timeout 0") {
        SessionFixture fx{"EVTESTID01", ev::SessionTiming{5ms, 0ms}};

        WHEN("the SAP request is sent and no response ever arrives") {
            fx.session.start();
            REQUIRE(run_reactor_until(
                fx.reactor, [&]() { return fx.captured.size() >= 1; }, 1s));

            THEN("nothing fires early, then the SupportedAppProtocol MESSAGE timeout expires") {
                REQUIRE_FALSE(run_reactor_until(
                    fx.reactor, [&]() { return fx.session.is_finished(); }, 200ms));
                REQUIRE_FALSE(fx.timed_out);

                REQUIRE(run_reactor_until(
                    fx.reactor, [&]() { return fx.session.is_finished(); }, 2500ms));
                REQUIRE(fx.timed_out_count == 1);
            }
        }
    }
}
