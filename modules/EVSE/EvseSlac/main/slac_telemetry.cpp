// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "slac_telemetry.hpp"

#include <fmt/format.h>
#include <fmt/ranges.h>

namespace module::main {

namespace api_telemetry = everest::lib::API::V1_0::types::telemetry;
using everest::lib::slac::D3State;
using everest::lib::slac::SlacFsmState;
using everest::lib::slac::SlacState;
using everest::lib::slac::SlacTelemetry;

namespace {

api_telemetry::SlacState to_api(SlacState state) {
    switch (state) {
    case SlacState::Init:
        return api_telemetry::SlacState::Init;
    case SlacState::Reset:
        return api_telemetry::SlacState::Reset;
    case SlacState::ResetChip:
        return api_telemetry::SlacState::ResetChip;
    case SlacState::Idle:
        return api_telemetry::SlacState::Idle;
    case SlacState::Failed:
        return api_telemetry::SlacState::Failed;
    case SlacState::Unmatched:
        return api_telemetry::SlacState::Unmatched;
    case SlacState::Matching:
        return api_telemetry::SlacState::Matching;
    case SlacState::WaitForLink:
        return api_telemetry::SlacState::WaitForLink;
    case SlacState::Validate:
        return api_telemetry::SlacState::Validate;
    case SlacState::Matched:
        return api_telemetry::SlacState::Matched;
    }
    return api_telemetry::SlacState::Init;
}

api_telemetry::SlacD3State to_api(D3State state) {
    switch (state) {
    case D3State::Unmatched:
        return api_telemetry::SlacD3State::Unmatched;
    case D3State::Matching:
        return api_telemetry::SlacD3State::Matching;
    case D3State::Matched:
        return api_telemetry::SlacD3State::Matched;
    }
    return api_telemetry::SlacD3State::Unmatched;
}

} // namespace

api_telemetry::SlacStatus to_api(SlacTelemetry const& status) {
    api_telemetry::SlacStatus telemetry;
    telemetry.matching_requested = status.matching_requested;
    telemetry.modem_PIB = status.modem_PIB;
    telemetry.modem_NMK = status.modem_NMK;
    telemetry.modem_link_ready = status.modem_link_ready;
    telemetry.session_count = status.session_count;
    telemetry.average_attenuation = status.average_attenuation;
    telemetry.ev_mac = fmt::format("{:02X}", fmt::join(status.ev_mac, ":"));
    telemetry.match_state = to_api(status.match_state);
    telemetry.d3_state = to_api(status.d3_state);
    return telemetry;
}

api_telemetry::SlacFsmState to_api(SlacFsmState const& fsm_state) {
    api_telemetry::SlacFsmState telemetry;
    telemetry.states = fsm_state.states;
    for (auto const& [state, submachine] : fsm_state.submachines) {
        telemetry.submachines[state] = to_api(submachine);
    }
    for (auto const& session : fsm_state.sessions) {
        telemetry.sessions.push_back(to_api(session));
    }
    return telemetry;
}

} // namespace module::main
