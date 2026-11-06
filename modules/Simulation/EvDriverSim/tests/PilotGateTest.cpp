// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "main/PilotGate.hpp"
#include <catch2/catch_test_macros.hpp>

using module::PilotGate;
using types::board_support_common::Event;
using types::ev_board_support::EvCpState;

TEST_CASE("EvDriverSim forwards car CP only while plugged", "[evdriver]") {
    PilotGate gate(false);
    CHECK_FALSE(gate.on_car_set_cp(EvCpState::C).to_board_cp);
    gate.plug();
    CHECK(gate.on_car_set_cp(EvCpState::B).to_board_cp == EvCpState::B);
    CHECK(gate.on_car_set_cp(EvCpState::C).to_board_cp == EvCpState::C);
    gate.unplug();
    CHECK_FALSE(gate.on_car_set_cp(EvCpState::E).to_board_cp);
}

TEST_CASE("EvDriverSim withholds board events at startup", "[evdriver]") {
    PilotGate gate(false);
    CHECK_FALSE(gate.passed());
    for (const auto event : {Event::A, Event::B, Event::C}) {
        const auto out = gate.on_board_event({event});
        CHECK(out.to_car_events.empty());
        CHECK_FALSE(out.to_board_cp);
    }
}

TEST_CASE("EvDriverSim plug announces A before passing board events", "[evdriver]") {
    PilotGate gate(false);
    const auto out = gate.plug();
    REQUIRE(out.to_car_events.size() == 1);
    CHECK(out.to_car_events.front().event == Event::A);
    CHECK_FALSE(out.to_board_cp);
    CHECK(gate.passed());
    for (const auto event : {Event::A, Event::B, Event::C, Event::E, Event::F, Event::PowerOn}) {
        const auto forwarded = gate.on_board_event({event});
        REQUIRE(forwarded.to_car_events.size() == 1);
        CHECK(forwarded.to_car_events.front().event == event);
    }
}

TEST_CASE("EvDriverSim unplug disconnects the car and forces board A", "[evdriver]") {
    PilotGate gate(true);
    CHECK(gate.passed());
    const auto out = gate.unplug();
    REQUIRE(out.to_car_events.size() == 1);
    CHECK(out.to_car_events.front().event == Event::Disconnected);
    CHECK(out.to_board_cp == EvCpState::A);
    CHECK_FALSE(gate.passed());
    CHECK(gate.on_board_event({Event::B}).to_car_events.empty());
}

TEST_CASE("EvDriverSim board reset restores withheld CP without car events", "[evdriver]") {
    for (const auto passed : {false, true}) {
        const PilotGate gate(passed);
        const auto out = gate.on_board_reset();
        CHECK(out.to_car_events.empty());
        CHECK(out.to_board_cp == (passed ? std::nullopt : std::make_optional(EvCpState::A)));
        CHECK(gate.passed() == passed);
    }
}
