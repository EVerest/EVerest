// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "lifecycle/codec.hpp"
#include "lifecycle/API.hpp"
#include "lifecycle/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"
#include <string>

namespace everest::lib::API::V1_0::types::lifecycle {

std::string serialize(StopModulesResultEnum val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(StartModulesResultEnum val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ModuleExecutionStatusEnum val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(StopModulesResult const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(StartModulesResult const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ConfigurationApiAvailability val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ExecutionStatusUpdateNotice const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(EVerestVersion const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::ostream& operator<<(std::ostream& os, StopModulesResultEnum const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, StartModulesResultEnum const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ModuleExecutionStatusEnum const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, StopModulesResult const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, StartModulesResult const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ConfigurationApiAvailability const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ExecutionStatusUpdateNotice const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, EVerestVersion const& val) {
    os << serialize(val);
    return os;
}

template <> StopModulesResultEnum deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> StartModulesResultEnum deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ModuleExecutionStatusEnum deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> StopModulesResult deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> StartModulesResult deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ConfigurationApiAvailability deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ExecutionStatusUpdateNotice deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> EVerestVersion deserialize<>(std::string const& val) {
    return json::parse(val);
}

} // namespace everest::lib::API::V1_0::types::lifecycle
