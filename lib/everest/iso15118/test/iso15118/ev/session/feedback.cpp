// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>
#include <variant>
#include <vector>

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

SCENARIO("ISO15118-20 EV Feedback session-level signals") {
    feedback::Callbacks callbacks;

    GIVEN("signal, selected_protocol, evse_id and pause_from_charger callbacks are set") {
        std::vector<feedback::Signal> signals;
        std::optional<iso15118::ProtocolId> protocol;
        std::string evse_id;
        int pause_calls = 0;
        callbacks.signal = [&signals](feedback::Signal signal) { signals.push_back(signal); };
        callbacks.selected_protocol = [&protocol](iso15118::ProtocolId id) { protocol = id; };
        callbacks.evse_id = [&evse_id](const std::string& id) { evse_id = id; };
        callbacks.pause_from_charger = [&pause_calls]() { ++pause_calls; };
        const auto feedback = Feedback(callbacks);

        WHEN("each is invoked") {
            feedback.signal(feedback::Signal::DLINK_PAUSE);
            feedback.signal(feedback::Signal::DLINK_ERROR);
            feedback.selected_protocol(iso15118::ProtocolId::ISO15118_2);
            feedback.evse_id("DE*PNX*E12345");
            feedback.pause_from_charger();

            THEN("every value reaches its callback verbatim") {
                REQUIRE(signals ==
                        std::vector<feedback::Signal>{feedback::Signal::DLINK_PAUSE, feedback::Signal::DLINK_ERROR});
                REQUIRE(protocol == iso15118::ProtocolId::ISO15118_2);
                REQUIRE(evse_id == "DE*PNX*E12345");
                REQUIRE(pause_calls == 1);
            }
        }
    }

    GIVEN("the dc_evse_present_limits callback is set") {
        std::optional<feedback::DcMaximumLimits> limits;
        callbacks.dc_evse_present_limits = [&limits](const feedback::DcMaximumLimits& l) { limits = l; };
        const auto feedback = Feedback(callbacks);

        WHEN("limits are reported") {
            feedback.dc_evse_present_limits({400.0f, 125.0f, 50000.0f});

            THEN("the callback receives them unchanged") {
                REQUIRE(limits.has_value());
                REQUIRE(limits->voltage == 400.0f);
                REQUIRE(limits->current == 125.0f);
                REQUIRE(limits->power == 50000.0f);
            }
        }
    }

    GIVEN("the v2g_message callback is set") {
        std::optional<iso15118::V2gMessageType> received;
        callbacks.v2g_message = [&received](const iso15118::V2gMessageType& type) { received = type; };
        const auto feedback = Feedback(callbacks);

        WHEN("a -20 message type is reported") {
            feedback.v2g_message(iso15118::message_20::Type::SessionSetupRes);

            THEN("the variant carries the -20 type") {
                REQUIRE(received.has_value());
                REQUIRE(std::holds_alternative<iso15118::message_20::Type>(*received));
                REQUIRE(std::get<iso15118::message_20::Type>(*received) == iso15118::message_20::Type::SessionSetupRes);
            }
        }

        WHEN("an ISO 15118-2 message type is reported") {
            feedback.v2g_message(iso15118::message_2::Type::SessionSetupRes);

            THEN("the variant carries the -2 type") {
                REQUIRE(received.has_value());
                REQUIRE(std::holds_alternative<iso15118::message_2::Type>(*received));
            }
        }
    }

    GIVEN("no session-level callbacks are set") {
        const auto feedback = Feedback(callbacks);

        THEN("invoking each signal is a safe no-op") {
            REQUIRE_NOTHROW(feedback.signal(feedback::Signal::DLINK_TERMINATE));
            REQUIRE_NOTHROW(feedback.selected_protocol(iso15118::ProtocolId::ISO15118_20));
            REQUIRE_NOTHROW(feedback.evse_id("DE*PNX*E12345"));
            REQUIRE_NOTHROW(feedback.pause_from_charger());
            REQUIRE_NOTHROW(feedback.dc_evse_present_limits({}));
            REQUIRE_NOTHROW(feedback.v2g_message(iso15118::message_20::Type::SessionStopRes));
        }
    }
}
