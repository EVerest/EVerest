// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include "yaml_provider_ifc.hpp"

namespace everest::config_cli {

class YamlProvider : public YamlProviderIfc {
public:
    std::string extract_active_modules_string(const std::filesystem::path& path) override;
    std::string format_configuration(const GetConfigurationResult& config) override;
    ConfigurationParameterUpdateRequest parse_parameter_updates(int slot_id,
                                                                const std::filesystem::path& path) override;
    GetConfigurationParameterRequest parse_parameter_requests(int slot_id,
                                                              const std::filesystem::path& path) override;
};

} // namespace everest::config_cli
