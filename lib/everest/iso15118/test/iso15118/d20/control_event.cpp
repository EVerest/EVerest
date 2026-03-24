// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/d20/control_event_queue.hpp>

SCENARIO("Control Event Tests") {

    GIVEN("pop() empty queue") {
        auto control_event_queue = iso15118::d20::ControlEventQueue{};

        REQUIRE(control_event_queue.pop() == std::nullopt);
    }

    GIVEN("push() and pop() from queue") {
        auto control_event_queue = iso15118::d20::ControlEventQueue{};
        control_event_queue.push(iso15118::d20::StopCharging{true});
        const auto event = control_event_queue.pop();
        REQUIRE(event.has_value() == true);
        REQUIRE(std::holds_alternative<iso15118::d20::StopCharging>(event.value()) == true);
        REQUIRE(*std::get_if<iso15118::d20::StopCharging>(&event.value()) == true);
    }

    GIVEN("push() multiple events and check with pop the order") {
        auto control_event_queue = iso15118::d20::ControlEventQueue{};
        control_event_queue.push(iso15118::d20::StopCharging{true});
        control_event_queue.push(iso15118::d20::CableCheckFinished{false});
        auto event = control_event_queue.pop();
        REQUIRE(event.has_value() == true);
        REQUIRE(std::holds_alternative<iso15118::d20::StopCharging>(event.value()) == true);
        REQUIRE(*std::get_if<iso15118::d20::StopCharging>(&event.value()) == true);

        event = control_event_queue.pop();
        REQUIRE(event.has_value() == true);
        REQUIRE(std::holds_alternative<iso15118::d20::CableCheckFinished>(event.value()) == true);
        REQUIRE(*std::get_if<iso15118::d20::CableCheckFinished>(&event.value()) == false);
    }
}
