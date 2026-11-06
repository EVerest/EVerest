// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include "everest/slac/HomeplugMessage.hpp"
#include <map>
#include <string>
#include <vector>

namespace everest::lib::slac {
enum class SlacState {
    Init,
    Reset,
    ResetChip,
    Idle,
    Failed,
    Unmatched,
    Matching,
    WaitForLink,
    Validate, // BCB toggle algol
    Matched,
};

enum class D3State {
    Unmatched,
    Matching,
    Matched,
};

struct SlacTelemetry {
    bool matching_requested{false}; // enable / disable
    bool modem_PIB{false};          // modem configured (NMK set, chip reset done); set on reaching Idle
    bool modem_NMK{false};          // NMK has been set successfully
    bool modem_link_ready{false};   // link status is good.
    int session_count{0};           // number of active sounding sessions
    float average_attenuation{0.f};
    messages::HomeplugMessage::MacAddress ev_mac{};
    SlacState match_state{SlacState::Init};
    D3State d3_state{D3State::Unmatched};
};

// The active states of the state machine, nested as the machine is.
struct SlacFsmState {
    std::vector<std::string> states;                 // active states on this level
    std::map<std::string, SlacFsmState> submachines; // by the state that runs the submachine
    std::vector<SlacFsmState> sessions;              // one per matching session
};

} // namespace everest::lib::slac
