// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include "everest/slac/HomeplugMessage.hpp"
#include <string>
#include <nlohmann/json_fwd.hpp>

namespace everest::lib::slac {
enum class SlacState {
    Init,
    Reset,
    ResetChip,
    Idle,
    Failed,
    Unmatched,
    Matching,
    WairForLink,
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
    bool modem_PIB{false};        // Indicates modem configuration and firmware are good. FIXME (jh) Implicitly derived.
    bool modem_NMK{false};        // NMK has been set successfully
    bool modem_link_ready{false}; // link status is good.
    int session_count{0};         // number of active sounding sessions
    float average_attenuation{0.f};
    messages::HomeplugMessage::MacAddress ev_mac;
    SlacState match_state{SlacState::Init};
    D3State d3_state{D3State::Unmatched};
};

std::string serialize(SlacTelemetry val);
SlacTelemetry deserialize(std::string const& val);

} // namespace everest::lib::slac
