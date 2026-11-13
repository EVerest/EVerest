// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <everest_api_types/telemetry/json_codec.hpp>

#include "slac_telemetry.hpp"

namespace {

namespace slac = everest::lib::slac;
using module::main::to_api;

TEST(SlacTelemetry, ADefaultStatusHasAZeroEvMacAndTheInitialStates) {
    nlohmann::json const json = to_api(slac::SlacTelemetry{});
    EXPECT_EQ(json.at("ev_mac"), "00:00:00:00:00:00");
    EXPECT_EQ(json.at("match_state"), "Init");
    EXPECT_EQ(json.at("d3_state"), "Unmatched");
    EXPECT_EQ(json.at("matching_requested"), false);
    EXPECT_EQ(json.at("session_count"), 0);
}

TEST(SlacTelemetry, EveryStatusFieldIsPublished) {
    slac::SlacTelemetry status;
    status.matching_requested = true;
    status.modem_PIB = true;
    status.modem_NMK = true;
    status.modem_link_ready = true;
    status.session_count = 2;
    status.average_attenuation = 21.5f;
    status.ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x0F};
    status.match_state = slac::SlacState::Matching;
    status.d3_state = slac::D3State::Matching;

    nlohmann::json const json = to_api(status);
    EXPECT_EQ(json, nlohmann::json::parse(R"({
        "matching_requested": true, "modem_PIB": true, "modem_NMK": true, "modem_link_ready": true,
        "session_count": 2, "average_attenuation": 21.5, "ev_mac": "AA:BB:CC:DD:EE:0F",
        "match_state": "Matching", "d3_state": "Matching"
    })"));
}

TEST(SlacTelemetry, TheStateMachineStateKeepsItsNesting) {
    slac::SlacFsmState session;
    session.states = {"Sounding"};
    slac::SlacFsmState matching;
    matching.states = {"WaitForSession"};
    matching.sessions = {session};
    slac::SlacFsmState root;
    root.states = {"Matching"};
    root.submachines["Matching"] = matching;

    nlohmann::json const json = to_api(root);
    EXPECT_EQ(json, nlohmann::json::parse(R"({
        "states": ["Matching"],
        "submachines": {"Matching": {"states": ["WaitForSession"], "sessions": [{"states": ["Sounding"]}]}}
    })"));
}

} // namespace
