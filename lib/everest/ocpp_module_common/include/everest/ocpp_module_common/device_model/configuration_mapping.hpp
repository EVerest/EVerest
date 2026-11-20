// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <filesystem>
#include <vector>

#include <everest/ocpp_module_common/device_model/config_service_binding.hpp>
#include <ocpp/v2/init_device_model_db.hpp>

namespace ocpp_module_common::device_model {

/// \brief Loads explicit configuration mappings using the supplied JSON schema.
/// An empty mapping path selects no mappings without reading the schema; invalid files throw.
std::vector<ConfigParameterBinding> load_configuration_mappings(const std::filesystem::path& path,
                                                                const std::filesystem::path& schema_path);

/// \brief Selects explicit mappings and, optionally, automatic views of the remaining parameters.
/// Unknown parameters, repeated parameters and repeated targets throw std::invalid_argument.
std::vector<ConfigParameterBinding> build_configuration_views(
    const std::map<Everest::config::ModuleIdType, everest::config::ModuleConfigurationParameters>& module_configs,
    const std::map<std::string, ModuleTierMappings>& module_mappings,
    const std::vector<ConfigParameterBinding>& mappings, bool expose_unmapped);

/// \brief Removes explicit targets from component configs so their values belong only to ConfigService.
/// A target with a different datatype or without an Actual attribute throws std::invalid_argument.
void remove_mapped_configuration_variables(
    std::map<ocpp::v2::ComponentKey, std::vector<ocpp::v2::DeviceModelVariable>>& component_configs,
    const std::vector<ConfigParameterBinding>& mappings,
    const std::map<Everest::config::ModuleIdType, everest::config::ModuleConfigurationParameters>& module_configs);

/// \brief Logs the readable device model at verbose level. Write-only values are excluded by the report API.
void log_device_model(ocpp::v2::DeviceModelInterface& device_model);

} // namespace ocpp_module_common::device_model