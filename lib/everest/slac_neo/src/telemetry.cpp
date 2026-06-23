// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

#include "fsm/misc.hpp"
#include <everest/slac/telemetry.hpp>
#include <everest_api_types/telemetry/codec.hpp>

namespace everest::lib::slac {

namespace {
namespace api_telemetry = everest::lib::API::V1_0::types::telemetry;

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

SlacState from_api(api_telemetry::SlacState state) {
    switch (state) {
    case api_telemetry::SlacState::Init:
        return SlacState::Init;
    case api_telemetry::SlacState::Reset:
        return SlacState::Reset;
    case api_telemetry::SlacState::ResetChip:
        return SlacState::ResetChip;
    case api_telemetry::SlacState::Idle:
        return SlacState::Idle;
    case api_telemetry::SlacState::Failed:
        return SlacState::Failed;
    case api_telemetry::SlacState::Unmatched:
        return SlacState::Unmatched;
    case api_telemetry::SlacState::Matching:
        return SlacState::Matching;
    case api_telemetry::SlacState::WaitForLink:
        return SlacState::WaitForLink;
    case api_telemetry::SlacState::Validate:
        return SlacState::Validate;
    case api_telemetry::SlacState::Matched:
        return SlacState::Matched;
    }
    return SlacState::Init;
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

D3State from_api(api_telemetry::SlacD3State state) {
    switch (state) {
    case api_telemetry::SlacD3State::Unmatched:
        return D3State::Unmatched;
    case api_telemetry::SlacD3State::Matching:
        return D3State::Matching;
    case api_telemetry::SlacD3State::Matched:
        return D3State::Matched;
    }
    return D3State::Unmatched;
}

api_telemetry::SlacStatus to_api(SlacTelemetry const& status) {
    api_telemetry::SlacStatus telemetry;
    telemetry.matching_requested = status.matching_requested;
    telemetry.modem_PIB = status.modem_PIB;
    telemetry.modem_NMK = status.modem_NMK;
    telemetry.modem_link_ready = status.modem_link_ready;
    telemetry.session_count = status.session_count;
    telemetry.average_attenuation = status.average_attenuation;
    telemetry.ev_mac = format_mac_addr(status.ev_mac);
    telemetry.match_state = to_api(status.match_state);
    telemetry.d3_state = to_api(status.d3_state);
    return telemetry;
}

SlacTelemetry from_api(api_telemetry::SlacStatus const& status) {
    SlacTelemetry telemetry;
    telemetry.matching_requested = status.matching_requested;
    telemetry.modem_PIB = status.modem_PIB;
    telemetry.modem_NMK = status.modem_NMK;
    telemetry.modem_link_ready = status.modem_link_ready;
    telemetry.session_count = static_cast<int>(status.session_count);
    telemetry.average_attenuation = status.average_attenuation;
    telemetry.ev_mac.fill(0);
    if (auto parsed = parse_mac_addr(status.ev_mac); parsed) {
        telemetry.ev_mac = *parsed;
    }
    telemetry.match_state = from_api(status.match_state);
    telemetry.d3_state = from_api(status.d3_state);
    return telemetry;
}

} // namespace

std::string serialize(SlacTelemetry val) {
    return api_telemetry::serialize(to_api(val));
}

SlacTelemetry deserialize(std::string const& val) {
    return from_api(api_telemetry::deserialize<api_telemetry::SlacStatus>(val));
}

} // namespace everest::lib::slac
