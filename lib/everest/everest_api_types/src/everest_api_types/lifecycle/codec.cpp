// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "lifecycle/codec.hpp"
#include "lifecycle/API.hpp"
#include "lifecycle/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"
#include "utilities/json_codec_helpers.hpp"
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::lifecycle {

std::string serialize(StopModulesResultEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(StartModulesResultEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ModuleExecutionStatusEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(StopModulesResult const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(StartModulesResult const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ConfigurationApiAvailability val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ExecutionStatusUpdateNotice const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(EVerestVersion const& val) noexcept {
    return utilities::dump_json(val);
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

template <> StopModulesResultEnum deserialize(std::string_view val) {
    return utilities::parse_json<StopModulesResultEnum>(val);
}

template <> StartModulesResultEnum deserialize(std::string_view val) {
    return utilities::parse_json<StartModulesResultEnum>(val);
}

template <> ModuleExecutionStatusEnum deserialize(std::string_view val) {
    return utilities::parse_json<ModuleExecutionStatusEnum>(val);
}

template <> StopModulesResult deserialize(std::string_view val) {
    return utilities::parse_json<StopModulesResult>(val);
}

template <> StartModulesResult deserialize(std::string_view val) {
    return utilities::parse_json<StartModulesResult>(val);
}

template <> ConfigurationApiAvailability deserialize(std::string_view val) {
    return utilities::parse_json<ConfigurationApiAvailability>(val);
}

template <> ExecutionStatusUpdateNotice deserialize(std::string_view val) {
    return utilities::parse_json<ExecutionStatusUpdateNotice>(val);
}

template <> EVerestVersion deserialize(std::string_view val) {
    return utilities::parse_json<EVerestVersion>(val);
}

} // namespace everest::lib::API::V1_0::types::lifecycle
