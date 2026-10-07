// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/ocpp_module_common/custom_error_mapping_validation.hpp>

#include <set>
#include <sstream>
#include <utility>

#include <everest/ocpp_module_common/error_handling.hpp>
#include <everest/ocpp_module_common/error_mapping.hpp>
#include <everest/ocpp_module_common/error_placeholders.hpp>
#include <nlohmann/json.hpp>
#include <utils/error/error_type_map.hpp>

namespace ocpp_module_common::custom_error_mapping {

namespace {

constexpr std::size_t V16_INFO_MAX_LENGTH = 50;
constexpr std::size_t V2_TECH_INFO_MAX_LENGTH = 500;
constexpr auto DEFAULT_VARIABLE_NAME = "Problem";

std::string pointer(const Entry& entry, std::initializer_list<const char*> path) {
    auto ptr = nlohmann::json::json_pointer{} / entry.key.to_string();
    for (const auto* token : path) {
        ptr /= token;
    }
    return ptr.to_string();
}

Finding finding(Finding::Level level, const Entry& entry, std::string ptr, std::string message) {
    return {level, entry.key.to_string(), std::move(ptr), std::move(message)};
}

std::string error_namespace(const std::string& error_type) {
    return error_type.substr(0, error_type.find('/'));
}

void validate_text(const Entry& entry, const std::string& text, std::initializer_list<const char*> path,
                   std::size_t max_length, std::vector<Finding>& findings) {
    const auto check = check_error_placeholders(text);
    for (const auto& unknown : check.unknown) {
        findings.push_back(finding(Finding::Level::Warning, entry, pointer(entry, path),
                                   "unknown placeholder '" + unknown + "' is sent as written"));
    }
    if (check.unterminated) {
        findings.push_back(finding(Finding::Level::Warning, entry, pointer(entry, path),
                                   "unterminated placeholder, missing '}', is sent as written"));
    }
    if (check.static_length > max_length) {
        findings.push_back(finding(Finding::Level::Warning, entry, pointer(entry, path),
                                   "text without placeholders has " + std::to_string(check.static_length) +
                                       " characters and is truncated to " + std::to_string(max_length)));
    }
}

/// \returns the component name the built-in mapping reports an error on \p evse with
std::string built_in_component_name(const std::optional<ocpp::v2::EVSE>& evse) {
    return evse.has_value() ? EVSE_COMPONENT_NAME : CHARGING_STATION_COMPONENT_NAME;
}

std::string join_quoted(const std::set<std::string>& names) {
    std::string result;
    for (const auto& name : names) {
        result += (result.empty() ? "'" : " or '") + name + "'";
    }
    return result;
}

bool names_device_model_entry(const V2Identity& v2) {
    return v2.component_name.has_value() || v2.component_instance.has_value() || v2.variable_name.has_value() ||
           v2.variable_instance.has_value();
}

std::vector<std::optional<ocpp::v2::EVSE>> candidate_evses(const EvseTopology& topology) {
    std::vector<std::optional<ocpp::v2::EVSE>> candidates;
    const auto add = [&candidates](std::int32_t evse, std::optional<std::int32_t> connector) {
        ocpp::v2::EVSE value;
        value.id = evse;
        value.connectorId = connector;
        candidates.emplace_back(value);
    };
    candidates.emplace_back(std::nullopt);
    for (const auto& [evse, connectors] : topology) {
        add(evse, std::nullopt);
        for (std::int32_t connector = 1; connector <= connectors; ++connector) {
            add(evse, connector);
        }
    }
    return candidates;
}

} // namespace

std::optional<Everest::error::ErrorTypes> read_declared_error_types(const std::filesystem::path& errors_dir) {
    if (!std::filesystem::is_directory(errors_dir)) {
        return std::nullopt;
    }
    return Everest::error::ErrorTypeMap(errors_dir).get_error_types();
}

std::set<std::string> builtin_error_types() {
    std::set<std::string> types;
    for (const auto& [type, entry] : ocpp_module_common::MrecErrorMapping::entries()) {
        types.insert(type);
    }
    return types;
}

std::vector<Finding> validate_error_types(const CustomFileErrorMapping& mapping,
                                          const Everest::error::ErrorTypes& declared) {
    std::set<std::string> namespaces;
    for (const auto& [type, description] : declared) {
        namespaces.insert(error_namespace(type));
    }
    std::vector<Finding> findings;
    for (const auto& [key, entry] : mapping.entries()) {
        const auto error_ns = error_namespace(key.type);
        if (namespaces.count(error_ns) == 0) {
            findings.push_back(
                finding(Finding::Level::Error, entry, "",
                        "unknown error namespace '" + error_ns + "', no errors/" + error_ns + ".yaml declares it"));
        } else if (declared.count(key.type) == 0) {
            findings.push_back(
                finding(Finding::Level::Error, entry, "",
                        "unknown error type '" + key.type + "', errors/" + error_ns + ".yaml does not declare it"));
        }
    }
    return findings;
}

std::vector<std::string> replaced_builtin_entries(const CustomFileErrorMapping& mapping,
                                                  const std::set<std::string>& builtin) {
    std::vector<std::string> replaced;
    for (const auto& [key, entry] : mapping.entries()) {
        if (!key.sub_type.has_value() && builtin.count(key.type) > 0) {
            replaced.push_back(key.to_string());
        }
    }
    return replaced;
}

std::vector<Finding> validate_values(const CustomFileErrorMapping& mapping) {
    std::vector<Finding> findings;
    for (const auto& [key, entry] : mapping.entries()) {
        if (entry.v16.has_value() && entry.v16->info.has_value()) {
            validate_text(entry, entry.v16->info.value(), {"v16", "info"}, V16_INFO_MAX_LENGTH, findings);
        }
        if (entry.v2.has_value() && entry.v2->tech_info.has_value()) {
            validate_text(entry, entry.v2->tech_info.value(), {"v2", "tech_info"}, V2_TECH_INFO_MAX_LENGTH, findings);
        }
    }
    return findings;
}

std::vector<Finding> validate_device_model(const CustomFileErrorMapping& mapping,
                                           const DeviceModelLookupFunction& lookup, const EvseTopology& topology,
                                           bool strict) {
    std::vector<Finding> findings;
    for (const auto& [key, entry] : mapping.entries()) {
        if (!entry.v2.has_value() || !names_device_model_entry(entry.v2.value())) {
            continue;
        }
        const auto& v2 = entry.v2.value();
        ocpp::v2::Component component;
        if (v2.component_instance.has_value()) {
            component.instance = v2.component_instance.value();
        }
        ocpp::v2::Variable variable;
        variable.name = v2.variable_name.value_or(DEFAULT_VARIABLE_NAME);
        if (v2.variable_instance.has_value()) {
            variable.instance = v2.variable_instance.value();
        }

        bool component_known = false;
        bool known = false;
        std::set<std::string> component_names;
        for (const auto& evse : candidate_evses(topology)) {
            component.name = v2.component_name.value_or(built_in_component_name(evse));
            if (component.name.get() == CHARGING_STATION_COMPONENT_NAME && evse.has_value()) {
                continue;
            }
            component.evse = evse;
            component_names.insert(component.name.get());
            const auto result = lookup(component, variable);
            known = result == DeviceModelLookup::Known;
            component_known = component_known || result != DeviceModelLookup::UnknownComponent;
            if (known) {
                break;
            }
        }
        if (known) {
            continue;
        }

        const auto message = component_known ? "the device model has no variable '" + variable.name.get() +
                                                   "' on component " + join_quoted(component_names)
                                             : "the device model has no component " + join_quoted(component_names);
        findings.push_back(
            finding(strict ? Finding::Level::Error : Finding::Level::Warning, entry, pointer(entry, {"v2"}), message));
    }
    return findings;
}

} // namespace ocpp_module_common::custom_error_mapping
