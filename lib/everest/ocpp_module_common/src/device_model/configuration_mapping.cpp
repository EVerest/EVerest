// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/ocpp_module_common/device_model/configuration_mapping.hpp>

#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>
#include <tuple>

#include <everest/logging.hpp>
#include <everest/utils/yaml_loader.hpp>
#include <nlohmann/json-schema.hpp>
#include <ocpp/common/utils.hpp>
#include <ocpp/v2/comparators.hpp>

namespace ocpp_module_common::device_model {
namespace {

using ParameterKey = std::tuple<std::string, std::string, std::string>;

ParameterKey key_of(const everest::config::ConfigurationParameterIdentifier& parameter) {
    return {parameter.module_id, parameter.module_implementation_id.value_or(Everest::config::MODULE_IMPLEMENTATION_ID),
            parameter.configuration_parameter_name};
}

void validate_target(const ConfigParameterBinding& mapping) {
    const auto& component = mapping.component;
    const auto& variable = mapping.variable;
    if (ocpp::iequals(component.name.get(), "ISO15118Ctrlr") && !component.evse &&
        (ocpp::iequals(variable.name.get(), "PnCEnabled") ||
         ocpp::iequals(variable.name.get(), "CentralContractValidationAllowed") ||
         ocpp::iequals(variable.name.get(), "ContractCertificateInstallationEnabled"))) {
        throw std::invalid_argument("Plug & Charge mappings require EVSE scope");
    }
    if (component.name.get().empty() || variable.name.get().empty() ||
        (component.instance.has_value() && component.instance->get().empty()) ||
        (variable.instance.has_value() && variable.instance->get().empty()) ||
        (component.evse.has_value() && (component.evse->id <= 0 || component.evse->connectorId.value_or(1) <= 0))) {
        throw std::invalid_argument("Invalid configuration mapping target");
    }
}

} // namespace

std::vector<ConfigParameterBinding> load_configuration_mappings(const std::filesystem::path& path,
                                                                const std::filesystem::path& schema_path) {
    if (path.empty()) {
        return {};
    }
    nlohmann::json_schema::json_validator validator;
    try {
        std::ifstream schema_file(schema_path);
        if (!schema_file.is_open()) {
            throw std::invalid_argument("Cannot open schema file");
        }
        validator.set_root_schema(nlohmann::json::parse(schema_file));
    } catch (const std::exception& e) {
        throw std::invalid_argument("Invalid configuration mapping schema " + schema_path.string() + ": " + e.what());
    }
    try {
        const auto document = Everest::load_yaml(path);
        validator.validate(document);
        const auto& entries = document.at("mappings");
        std::vector<ConfigParameterBinding> mappings;
        for (const auto& entry : entries) {
            const auto& everest = entry.at("everest");
            ConfigParameterBinding mapping;
            mapping.parameter.module_id = everest.at("module_id").get<std::string>();
            mapping.parameter.configuration_parameter_name = everest.at("parameter").get<std::string>();
            if (everest.contains("implementation_id")) {
                mapping.parameter.module_implementation_id = everest.at("implementation_id").get<std::string>();
            }
            const auto& ocpp = entry.at("ocpp");
            mapping.component = nlohmann::json(ocpp.at("component")).get<ocpp::v2::Component>();
            mapping.variable = nlohmann::json(ocpp.at("variable")).get<ocpp::v2::Variable>();
            validate_target(mapping);
            mappings.push_back(std::move(mapping));
        }
        return mappings;
    } catch (const std::exception& e) {
        throw std::invalid_argument("Invalid configuration mapping file " + path.string() + ": " + e.what());
    }
}

std::vector<ConfigParameterBinding> build_configuration_views(
    const std::map<Everest::config::ModuleIdType, everest::config::ModuleConfigurationParameters>& module_configs,
    const std::map<std::string, ModuleTierMappings>& module_mappings,
    const std::vector<ConfigParameterBinding>& mappings, const bool expose_unmapped) {
    std::map<ParameterKey, const everest::config::ConfigurationParameter*> parameters;
    for (const auto& [module, implementations] : module_configs) {
        for (const auto& [implementation, entries] : implementations) {
            for (const auto& entry : entries) {
                parameters[{module.module_id, implementation, entry.name}] = &entry;
            }
        }
    }
    std::set<ParameterKey> mapped;
    std::set<std::pair<ocpp::v2::Component, ocpp::v2::Variable>> targets;
    auto views = mappings;
    for (const auto& mapping : mappings) {
        validate_target(mapping);
        const auto key = key_of(mapping.parameter);
        if (parameters.count(key) == 0) {
            throw std::invalid_argument("Unknown configuration mapping source: " + mapping.parameter.module_id + "." +
                                        mapping.parameter.configuration_parameter_name);
        }
        to_variable_characteristics(parameters.at(key)->characteristics);
        if (!mapped.insert(key).second) {
            throw std::invalid_argument("Configuration parameter is mapped more than once");
        }
        if (!targets.emplace(mapping.component, mapping.variable).second) {
            throw std::invalid_argument("Configuration mapping target is used more than once");
        }
    }
    if (expose_unmapped) {
        for (auto& view : build_module_configuration_views(module_configs, module_mappings)) {
            if (mapped.count(key_of(view.parameter)) != 0) {
                continue;
            }
            if (!targets.emplace(view.component, view.variable).second) {
                throw std::invalid_argument("Configuration mapping target conflicts with an automatic view");
            }
            views.push_back(std::move(view));
        }
    }
    return views;
}

void remove_mapped_configuration_variables(
    std::map<ocpp::v2::ComponentKey, std::vector<ocpp::v2::DeviceModelVariable>>& component_configs,
    const std::vector<ConfigParameterBinding>& mappings,
    const std::map<Everest::config::ModuleIdType, everest::config::ModuleConfigurationParameters>& module_configs) {
    build_configuration_views(module_configs, {}, mappings, false);
    if (mappings.empty()) {
        return;
    }
    std::map<ParameterKey, ocpp::v2::DataEnum> types;
    std::set<ParameterKey> mapped_parameters;
    for (const auto& mapping : mappings) {
        mapped_parameters.insert(key_of(mapping.parameter));
    }
    for (const auto& [module, implementations] : module_configs) {
        for (const auto& [implementation, entries] : implementations) {
            for (const auto& entry : entries) {
                const ParameterKey key{module.module_id, implementation, entry.name};
                if (mapped_parameters.count(key) != 0) {
                    types[key] = to_variable_characteristics(entry.characteristics).dataType;
                }
            }
        }
    }
    for (const auto& mapping : mappings) {
        ocpp::v2::ComponentKey component;
        component.name = mapping.component.name.get();
        if (mapping.component.instance.has_value()) {
            component.instance = mapping.component.instance->get();
        }
        if (mapping.component.evse.has_value()) {
            component.evse_id = mapping.component.evse->id;
            component.connector_id = mapping.component.evse->connectorId;
        }
        const auto found = component_configs.find(component);
        if (found == component_configs.end()) {
            continue;
        }
        ocpp::v2::DeviceModelVariable target;
        target.name = mapping.variable.name.get();
        if (mapping.variable.instance.has_value()) {
            target.instance = mapping.variable.instance->get();
        }
        auto& variables = found->second;
        const auto variable = std::find_if(variables.begin(), variables.end(), [&](const auto& candidate) {
            return ocpp::v2::is_same_variable(candidate, target);
        });
        if (variable == variables.end()) {
            continue;
        }
        const auto actual = std::find_if(variable->attributes.begin(), variable->attributes.end(), [](const auto& a) {
            return a.variable_attribute.type.value_or(ocpp::v2::AttributeEnum::Actual) ==
                   ocpp::v2::AttributeEnum::Actual;
        });
        if (variable->characteristics.dataType != types.at(key_of(mapping.parameter)) ||
            actual == variable->attributes.end()) {
            throw std::invalid_argument("Configuration mapping target has an incompatible definition: " + target.name);
        }
        variables.erase(variable);
    }
}

void remove_unscoped_pnc_variables(
    std::map<ocpp::v2::ComponentKey, std::vector<ocpp::v2::DeviceModelVariable>>& component_configs) {
    for (auto& [component, variables] : component_configs) {
        if (!ocpp::iequals(component.name, "ISO15118Ctrlr") || component.evse_id.has_value()) {
            continue;
        }
        variables.erase(std::remove_if(variables.begin(), variables.end(),
                                       [](const auto& variable) {
                                           return ocpp::iequals(variable.name, "PnCEnabled") ||
                                                  ocpp::iequals(variable.name, "CentralContractValidationAllowed") ||
                                                  ocpp::iequals(variable.name,
                                                                "ContractCertificateInstallationEnabled");
                                       }),
                        variables.end());
    }
}

void log_device_model(ocpp::v2::DeviceModelInterface& device_model) {
    const auto report = device_model.get_base_report_data(ocpp::v2::ReportBaseEnum::FullInventory);
    EVLOG_verbose << "Composed device model: " << report.size() << " component-variable entries";
    for (const auto& entry : report) {
        EVLOG_verbose << "Device model: " << nlohmann::json(entry).dump();
    }
}

} // namespace ocpp_module_common::device_model