// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/slac/status.hpp>

#include <everest/slac/protocol/format.hpp>
#include <everest_api_types/telemetry/codec.hpp>

namespace everest::slac {

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
    case SlacState::Matching:
        return api_telemetry::SlacState::Matching;
    case SlacState::WaitForLink:
        return api_telemetry::SlacState::WaitForLink;
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

api_telemetry::SlacStatus to_api(Status const& status) {
    api_telemetry::SlacStatus out;
    out.modem_PIB = status.modem_PIB;
    out.modem_NMK = status.modem_NMK;
    out.modem_link_ready = status.modem_link_ready;
    out.session_count = status.session_count;
    out.average_attenuation = status.average_attenuation;
    out.ev_mac = format_mac_addr(status.ev_mac);
    out.match_state = to_api(status.match_state);
    out.d3_state = to_api(status.d3_state);
    // matching_requested is left at its default: neither implementation ever set it
    return out;
}

} // namespace

std::string to_string(D3State state) {
    switch (state) {
    case D3State::Unmatched:
        return "UNMATCHED";
    case D3State::Matching:
        return "MATCHING";
    case D3State::Matched:
        return "MATCHED";
    }
    return "UNMATCHED";
}

std::string serialize(Status const& val) {
    return api_telemetry::serialize(to_api(val));
}

} // namespace everest::slac
