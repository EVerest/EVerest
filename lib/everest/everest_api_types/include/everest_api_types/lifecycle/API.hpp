// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <optional>
#include <string>

namespace everest::lib::API::V1_0::types::lifecycle {

enum class StopModulesResultEnum {
    Stopping,
    NoModulesToStop,
    Rejected,
};

enum class StartModulesResultEnum {
    Starting,
    Restarting,
    NoConfigToStart,
    Rejected,
};

enum class ModuleExecutionStatusEnum {
    Running,
    NotRunning,
    Starting,
    Stopping,
    RestartTriggered
};

struct StopModulesResult {
    StopModulesResultEnum status;
};

struct StartModulesResult {
    StartModulesResultEnum status;
};

enum class ConfigurationApiAvailability {
    N_A,
    RO,
    RW,
};

struct ExecutionStatusUpdateNotice {
    std::optional<std::string> tstamp{};
    bool everest_running;
    std::optional<ModuleExecutionStatusEnum> module_status;
    std::optional<ConfigurationApiAvailability> configuration_api_available;
    std::optional<bool> lifecycle_api_ro;
};

struct EVerestVersion {
    std::string name;
    std::string version;
    std::string git_version;
};

} // namespace everest::lib::API::V1_0::types::lifecycle
