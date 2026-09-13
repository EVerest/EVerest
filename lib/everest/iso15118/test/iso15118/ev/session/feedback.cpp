// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/ev/session/feedback.hpp>

using namespace iso15118::ev;

SCENARIO("ISO15118-20 EV Feedback DC charge-loop signals") {
    feedback::Callbacks callbacks;

    GIVEN("ev_power_ready callback is set") {
        int calls = 0;
        callbacks.ev_power_ready = [&calls]() { ++calls; };
        const auto feedback = Feedback(callbacks);

        WHEN("ev_power_ready is invoked") {
            feedback.ev_power_ready();
            THEN("the callback fires exactly once") {
                REQUIRE(calls == 1);
            }
        }
    }

    GIVEN("dc_power_on callback is set") {
        int calls = 0;
        callbacks.dc_power_on = [&calls]() { ++calls; };
        const auto feedback = Feedback(callbacks);

        WHEN("dc_power_on is invoked") {
            feedback.dc_power_on();
            THEN("the callback fires exactly once") {
                REQUIRE(calls == 1);
            }
        }
    }

    GIVEN("stop_from_charger callback is set") {
        int calls = 0;
        callbacks.stop_from_charger = [&calls]() { ++calls; };
        const auto feedback = Feedback(callbacks);

        WHEN("stop_from_charger is invoked") {
            feedback.stop_from_charger();
            THEN("the callback fires exactly once") {
                REQUIRE(calls == 1);
            }
        }
    }

    GIVEN("no DC charge-loop callbacks are set") {
        const auto feedback = Feedback(callbacks);

        THEN("invoking each signal is a safe no-op") {
            REQUIRE_NOTHROW(feedback.ev_power_ready());
            REQUIRE_NOTHROW(feedback.dc_power_on());
            REQUIRE_NOTHROW(feedback.stop_from_charger());
        }
    }
}
