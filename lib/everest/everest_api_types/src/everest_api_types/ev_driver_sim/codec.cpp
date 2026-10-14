// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "ev_driver_sim/codec.hpp"
#include "ev_driver_sim/API.hpp"
#include "ev_driver_sim/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::ev_driver_sim {

std::string serialize(WorldFault val) {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(PilotState val) {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(CommandAckStatus val) {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(InjectFaultParams const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(WorldState const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(CommandAck const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::ostream& operator<<(std::ostream& os, WorldFault const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, PilotState const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, CommandAckStatus const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, InjectFaultParams const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, WorldState const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, CommandAck const& val) {
    os << serialize(val);
    return os;
}

template <> WorldFault deserialize(std::string_view val) {
    return json::parse(val.begin(), val.end());
}

template <> PilotState deserialize(std::string_view val) {
    return json::parse(val.begin(), val.end());
}

template <> CommandAckStatus deserialize(std::string_view val) {
    return json::parse(val.begin(), val.end());
}

template <> InjectFaultParams deserialize(std::string_view val) {
    return json::parse(val.begin(), val.end());
}

template <> WorldState deserialize(std::string_view val) {
    return json::parse(val.begin(), val.end());
}

template <> CommandAck deserialize(std::string_view val) {
    return json::parse(val.begin(), val.end());
}

std::string serialize(ScenarioName val) {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ScenarioTimingOverrides const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(RunScenarioParams const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::ostream& operator<<(std::ostream& os, ScenarioName const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ScenarioTimingOverrides const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, RunScenarioParams const& val) {
    os << serialize(val);
    return os;
}

template <> ScenarioName deserialize(std::string_view val) {
    return json::parse(val.begin(), val.end());
}

template <> ScenarioTimingOverrides deserialize(std::string_view val) {
    return json::parse(val.begin(), val.end());
}

template <> RunScenarioParams deserialize(std::string_view val) {
    return json::parse(val.begin(), val.end());
}

std::string serialize(ScenarioEventKind val) {
    return nlohmann::json(val).dump(json_indent);
}
std::ostream& operator<<(std::ostream& os, ScenarioEventKind const& val) {
    os << serialize(val);
    return os;
}
template <> ScenarioEventKind deserialize(std::string_view val) {
    return json::parse(val.begin(), val.end());
}
std::string serialize(ScenarioEvent const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}
std::ostream& operator<<(std::ostream& os, ScenarioEvent const& val) {
    os << serialize(val);
    return os;
}
template <> ScenarioEvent deserialize(std::string_view val) {
    return json::parse(val.begin(), val.end());
}

} // namespace everest::lib::API::V1_0::types::ev_driver_sim
