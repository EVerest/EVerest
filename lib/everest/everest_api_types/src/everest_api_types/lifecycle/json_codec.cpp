// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "lifecycle/json_codec.hpp"
#include "lifecycle/API.hpp"
#include "lifecycle/codec.hpp"

#include "nlohmann/json.hpp"
#include <stdexcept>

namespace everest::lib::API::V1_0::types::lifecycle {

void to_json(json& j, StopModulesResultEnum const& k) noexcept {
    switch (k) {
    case StopModulesResultEnum::Stopping:
        j = "Stopping";
        return;
    case StopModulesResultEnum::NoModulesToStop:
        j = "NoModulesToStop";
        return;
    case StopModulesResultEnum::Rejected:
        j = "Rejected";
        return;
    }

    j = "INVALID_VALUE__everest::lib::API::V1_0::types::lifecycle::StopModulesResultEnum";
}

void from_json(const json& j, StopModulesResultEnum& k) {
    std::string s = j;
    if (s == "Stopping") {
        k = StopModulesResultEnum::Stopping;
        return;
    }
    if (s == "NoModulesToStop") {
        k = StopModulesResultEnum::NoModulesToStop;
        return;
    }
    if (s == "Rejected") {
        k = StopModulesResultEnum::Rejected;
        return;
    }

    throw std::out_of_range("Provided string " + s +
                            " could not be converted to enum of type "
                            "everest::lib::API::V1_0::types::lifecycle::StopModulesResultEnum");
}

void to_json(json& j, StartModulesResultEnum const& k) noexcept {
    switch (k) {
    case StartModulesResultEnum::Starting:
        j = "Starting";
        return;
    case StartModulesResultEnum::Restarting:
        j = "Restarting";
        return;
    case StartModulesResultEnum::NoConfigToStart:
        j = "NoConfigToStart";
        return;
    case StartModulesResultEnum::Rejected:
        j = "Rejected";
        return;
    }

    j = "INVALID_VALUE__everest::lib::API::V1_0::types::lifecycle::StartModulesResultEnum";
}

void from_json(const json& j, StartModulesResultEnum& k) {
    std::string s = j;
    if (s == "Starting") {
        k = StartModulesResultEnum::Starting;
        return;
    }
    if (s == "Restarting") {
        k = StartModulesResultEnum::Restarting;
        return;
    }
    if (s == "NoConfigToStart") {
        k = StartModulesResultEnum::NoConfigToStart;
        return;
    }
    if (s == "Rejected") {
        k = StartModulesResultEnum::Rejected;
        return;
    }

    throw std::out_of_range("Provided string " + s +
                            " could not be converted to enum of type "
                            "everest::lib::API::V1_0::types::lifecycle::StartModulesResultEnum");
}

void to_json(json& j, ModuleExecutionStatusEnum const& k) noexcept {
    switch (k) {
    case ModuleExecutionStatusEnum::Running:
        j = "Running";
        return;
    case ModuleExecutionStatusEnum::NotRunning:
        j = "NotRunning";
        return;
    case ModuleExecutionStatusEnum::Starting:
        j = "Starting";
        return;
    case ModuleExecutionStatusEnum::Stopping:
        j = "Stopping";
        return;
    case ModuleExecutionStatusEnum::RestartTriggered:
        j = "RestartTriggered";
        return;
    }

    j = "INVALID_VALUE__everest::lib::API::V1_0::types::lifecycle::ModuleExecutionStatusEnum";
}

void from_json(const json& j, ModuleExecutionStatusEnum& k) {
    std::string s = j;
    if (s == "Running") {
        k = ModuleExecutionStatusEnum::Running;
        return;
    }
    if (s == "NotRunning") {
        k = ModuleExecutionStatusEnum::NotRunning;
        return;
    }
    if (s == "Starting") {
        k = ModuleExecutionStatusEnum::Starting;
        return;
    }
    if (s == "Stopping") {
        k = ModuleExecutionStatusEnum::Stopping;
        return;
    }
    if (s == "RestartTriggered") {
        k = ModuleExecutionStatusEnum::RestartTriggered;
        return;
    }

    throw std::out_of_range(
        "Provided string " + s +
        " could not be converted to enum of type everest::lib::API::V1_0::types::lifecycle::ModuleExecutionStatusEnum");
}

void to_json(json& j, StopModulesResult const& k) noexcept {
    j = json{{"status", k.status}};
}

void from_json(const json& j, StopModulesResult& k) {
    k.status = j.at("status");
}

void to_json(json& j, StartModulesResult const& k) noexcept {
    j = json{{"status", k.status}};
}

void from_json(const json& j, StartModulesResult& k) {
    k.status = j.at("status");
}

void to_json(json& j, ConfigurationApiAvailability const& k) noexcept {
    switch (k) {
    case ConfigurationApiAvailability::N_A:
        j = "N_A";
        return;
    case ConfigurationApiAvailability::RO:
        j = "RO";
        return;
    case ConfigurationApiAvailability::RW:
        j = "RW";
        return;
    }

    j = "INVALID_VALUE__everest::lib::API::V1_0::types::lifecycle::ConfigurationApiAvailability";
}

void from_json(const json& j, ConfigurationApiAvailability& k) {
    std::string s = j;
    if (s == "N_A") {
        k = ConfigurationApiAvailability::N_A;
        return;
    }
    if (s == "RO") {
        k = ConfigurationApiAvailability::RO;
        return;
    }
    if (s == "RW") {
        k = ConfigurationApiAvailability::RW;
        return;
    }

    throw std::out_of_range("Provided string " + s +
                            " could not be converted to enum of type "
                            "everest::lib::API::V1_0::types::lifecycle::ConfigurationApiAvailability");
}

void to_json(json& j, ExecutionStatusUpdateNotice const& k) noexcept {
    j = json{{"everest_running", k.everest_running}};
    if (k.tstamp) {
        j["tstamp"] = *k.tstamp;
    }
    if (k.module_status) {
        j["module_status"] = *k.module_status;
    }
    if (k.configuration_api_available) {
        j["configuration_api_available"] = *k.configuration_api_available;
    }
    if (k.lifecycle_api_ro) {
        j["lifecycle_api_ro"] = *k.lifecycle_api_ro;
    }
}

void from_json(const json& j, ExecutionStatusUpdateNotice& k) {
    if (j.contains("tstamp")) {
        k.tstamp = j.at("tstamp");
    }
    k.everest_running = j.at("everest_running");
    if (j.contains("module_status")) {
        k.module_status = j.at("module_status");
    }
    if (j.contains("configuration_api_available")) {
        k.configuration_api_available = j.at("configuration_api_available");
    }
    if (j.contains("lifecycle_api_ro")) {
        k.lifecycle_api_ro = j.at("lifecycle_api_ro");
    }
}

void to_json(json& j, EVerestVersion const& k) noexcept {
    j = json{
        {"name", k.name},
        {"version", k.version},
        {"git_version", k.git_version},
    };
}

void from_json(const json& j, EVerestVersion& k) {
    k.name = j.at("name");
    k.version = j.at("version");
    k.git_version = j.at("git_version");
}

} // namespace everest::lib::API::V1_0::types::lifecycle
