// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

#include "fsm/misc.hpp"
#include <everest/slac/telemetry.hpp>
#include <nlohmann/json.hpp>

namespace everest::lib::slac {

using json = nlohmann::json;
void from_json(const json& j, SlacState& k);
void to_json(json& j, const SlacState& k) noexcept;

void from_json(const json& j, D3State& k);
void to_json(json& j, const D3State& k) noexcept;

void from_json(const json& j, SlacTelemetry& k);
void to_json(json& j, const SlacTelemetry& k) noexcept;

void from_json(const json& j, SlacState& k) {
    std::string s = j;
    if (s == "Init") {
        k = SlacState::Init;
        return;
    }
    if (s == "Reset") {
        k = SlacState::Reset;
        return;
    }
    if (s == "ResetChip") {
        k = SlacState::ResetChip;
        return;
    }
    if (s == "Idle") {
        k = SlacState::Idle;
        return;
    }
    if (s == "Failed") {
        k = SlacState::Failed;
        return;
    }
    if (s == "Unmatched") {
        k = SlacState::Unmatched;
        return;
    }
    if (s == "Matching") {
        k = SlacState::Matching;
        return;
    }
    if (s == "WairForLink") {
        k = SlacState::WairForLink;
        return;
    }
    if (s == "Validate") {
        k = SlacState::Validate;
        return;
    }
    if (s == "Matched") {
        k = SlacState::Matched;
        return;
    }
    throw std::out_of_range("Provided string " + s +
                            " could not be converted to enum of type everest::lib::slac::SlacState");
}
void to_json(json& j, const SlacState& k) noexcept {
    switch (k) {
    case SlacState::Init:
        j = "Init";
        return;
    case SlacState::Reset:
        j = "Reset";
        return;
    case SlacState::ResetChip:
        j = "ResetChip";
        return;
    case SlacState::Idle:
        j = "Idle";
        return;
    case SlacState::Failed:
        j = "Failed";
        return;
    case SlacState::Unmatched:
        j = "Unmatched";
        return;
    case SlacState::Matching:
        j = "Matching";
        return;
    case SlacState::WairForLink:
        j = "WairForLink";
        return;
    case SlacState::Validate:
        j = "Validate";
        return;
    case SlacState::Matched:
        j = "Matched";
        return;
    }
    j = "INVALID_everest::lib::slac::SlacState";
    return;
}

void from_json(const json& j, D3State& k) {
    std::string s = j;
    if (s == "Unmatched") {
        k = D3State::Unmatched;
        return;
    }
    if (s == "Matching") {
        k = D3State::Matching;
        return;
    }
    if (s == "Matched") {
        k = D3State::Matched;
        return;
    }
    throw std::out_of_range("Provided string " + s +
                            " could not be converted to enum of type everest::lib::slac::D3State");
}
void to_json(json& j, const D3State& k) noexcept {
    switch (k) {
    case D3State::Unmatched:
        j = "Unmatched";
        return;
    case D3State::Matching:
        j = "Matching";
        return;
    case D3State::Matched:
        j = "Matched";
        return;
    }
    j = "INVALID_everest::lib::slac::D3State";
    return;
}

void from_json(const json& j, SlacTelemetry& k) {
    if(j.contains("matching_requested")) {
        k.matching_requested = j.at("matching_requested");
    }
    if (j.contains("modem_PIB")) {
        k.modem_PIB = j.at("modem_PIB");
    }
    if (j.contains("modem_NMK")) {
        k.modem_NMK = j.at("modem_NMK");
    }
    if (j.contains("modem_link_ready")) {
        k.modem_link_ready = j.at("modem_link_ready");
    }
    if (j.contains("session_count")) {
        k.session_count = j.at("session_count");
    }
    if (j.contains("average_attenuation")) {
        k.average_attenuation = j.at("average_attenuation");
    }
    if (j.contains("match_state")) {
        k.match_state = j.at("match_state");
    }
    if (j.contains("d3_state")) {
        k.d3_state = j.at("d3_state");
    }
    if (j.contains("ev_mac")) {
        parse_mac_addr(j["ev_mac"], k.ev_mac.data(), k.ev_mac.size());
    }
}
void to_json(json& j, const SlacTelemetry& k) noexcept {
    j["matching_requested"] = k.matching_requested;
    j["modem_PIB"] = k.modem_PIB;
    j["modem_NMK"] = k.modem_NMK;
    j["modem_link_ready"] = k.modem_link_ready;
    j["session_count"] = k.session_count;
    j["average_attenuation"] = k.average_attenuation;
    j["ev_mac"] = format_mac_addr(k.ev_mac.data());
    j["match_state"] = k.match_state;
    j["d3_state"] = k.d3_state;
}

std::string serialize(SlacTelemetry val) {
    return nlohmann::json(val).dump(0);
}
SlacTelemetry deserialize(std::string const& val) {
    return json::parse(val);
}

} // namespace everest::lib::slac
