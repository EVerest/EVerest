// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#pragma once
#include <optional>
#include <string>
#include <vector>

namespace everest::lib::API::V1_0::types::entrypoint {

struct ModuleAssociations {
    std::vector<std::string> required_modules;
};

struct CommunicationParameters {
    std::optional<int32_t> heartbeat_period_ms;
    std::optional<int32_t> communication_check_period_s;
    std::optional<int32_t> request_reply_timeout_s;
};

struct ApiParameter {
    std::string type;
    std::string module_id;
    int32_t version;
    std::optional<ModuleAssociations> associated_module;
    std::optional<CommunicationParameters> communication_monitoring;
};

struct ApiDiscoverResponse {
    std::vector<ApiParameter> apis;
};

struct ModuleParameter {
    std::string name;
    std::string module_id;
};

struct EVerestVersion {
    std::string name;
    std::string version;
    std::string git_version;
};

struct QueryEVerestConfigurationResponse {
    std::vector<ModuleParameter> modules;
    EVerestVersion version;
};

} // namespace everest::lib::API::V1_0::types::entrypoint
