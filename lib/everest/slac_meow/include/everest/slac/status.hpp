// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <string>

#include <everest/slac/protocol/types.hpp>

namespace everest::slac {

/// The EVSE machine's internal state, as reported in telemetry.
enum class SlacState {
    Init,
    Reset,
    ResetChip,
    Idle,
    Failed,
    Matching,
    WaitForLink,
    Matched,
};

/// The D-LINK state of ISO 15118-3 9.1 / Figure 11 - the one state visible outside the library.
enum class D3State {
    Unmatched,
    Matching,
    Matched,
};

/// Both machines publish a D3State; only the EVSE publishes the rest.
struct Status {
    bool modem_PIB{false}; // modem configuration and firmware are good; implicitly derived
    bool modem_NMK{false}; // the NMK has been set on the modem
    bool modem_link_ready{false};
    int session_count{0};
    float average_attenuation{0.f};
    MacAddress ev_mac{};
    SlacState match_state{SlacState::Init};
    D3State d3_state{D3State::Unmatched};
};

/// "UNMATCHED" / "MATCHING" / "MATCHED" - the spelling the slac interface uses.
std::string to_string(D3State state);

std::string serialize(Status const& val);

} // namespace everest::slac
