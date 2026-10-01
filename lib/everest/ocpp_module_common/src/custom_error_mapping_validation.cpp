// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/ocpp_module_common/custom_error_mapping_validation.hpp>

#include <sstream>
#include <string_view>
#include <utility>

#include <everest/ocpp_module_common/error_mapping.hpp>
#include <nlohmann/json.hpp>
#include <utils/error/error_type_map.hpp>

namespace ocpp_module_common::custom_error_mapping {

namespace {

constexpr std::string_view PLACEHOLDER_START = "${";
constexpr std::string_view ACTUAL_VALUE_PLACEHOLDER = "${actual_value}";
constexpr std::size_t V16_INFO_MAX_LENGTH = 50;
constexpr std::size_t V2_TECH_INFO_MAX_LENGTH = 500;
constexpr auto DEFAULT_COMPONENT_NAME = "EVSE";
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

std::string describe(const Mapping& mapping) {
    std::ostringstream out;
    out << "evse " << mapping.evse;
    if (mapping.connector.has_value()) {
        out << " connector " << mapping.connector.value();
    }
    return out.str();
}

void validate_text(const Entry& entry, const std::string& text, std::initializer_list<const char*> path,
                   std::size_t max_length, std::vector<Finding>& findings) {
    std::size_t static_length = 0;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const auto start = text.find(PLACEHOLDER_START, pos);
        if (start == std::string::npos) {
            static_length += text.size() - pos;
            break;
        }
        static_length += start - pos;
        const auto end = text.find('}', start);
        if (end == std::string::npos) {
            findings.push_back(
                finding(Finding::Level::Error, entry, pointer(entry, path), "unterminated placeholder, missing '}'"));
            return;
        }
        const auto placeholder = std::string_view(text).substr(start, end - start + 1);
        if (placeholder != ACTUAL_VALUE_PLACEHOLDER) {
            findings.push_back(finding(Finding::Level::Error, entry, pointer(entry, path),
                                       "unknown placeholder '" + std::string(placeholder) + "', only '" +
                                           std::string(ACTUAL_VALUE_PLACEHOLDER) + "' is supported"));
        }
        pos = end + 1;
    }
    if (static_length > max_length) {
        findings.push_back(finding(Finding::Level::Warning, entry, pointer(entry, path),
                                   "text without placeholders has " + std::to_string(static_length) +
                                       " characters and is truncated to " + std::to_string(max_length)));
    }
}

bool names_device_model_entry(const V2Identity& v2) {
    return v2.component_name.has_value() || v2.component_instance.has_value() || v2.variable_name.has_value() ||
           v2.variable_instance.has_value();
}

std::vector<std::optional<ocpp::v2::EVSE>> candidate_evses(const std::optional<Mapping>& mapping,
                                                           const EvseTopology& topology) {
    std::vector<std::optional<ocpp::v2::EVSE>> candidates;
    const auto add = [&candidates](std::int32_t evse, std::optional<std::int32_t> connector) {
        ocpp::v2::EVSE value;
        value.id = evse;
        value.connectorId = connector;
        candidates.emplace_back(value);
    };
    if (mapping.has_value()) {
        if (mapping->evse == 0) {
            candidates.emplace_back(std::nullopt);
            return candidates;
        }
        if (mapping->connector.has_value()) {
            add(mapping->evse, mapping->connector);
        }
        add(mapping->evse, std::nullopt);
        return candidates;
    }
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

std::vector<Finding> validate_error_types(const CustomErrorMapping& mapping,
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

std::vector<std::string> replaced_builtin_entries(const CustomErrorMapping& mapping,
                                                  const std::set<std::string>& builtin) {
    std::vector<std::string> replaced;
    for (const auto& [key, entry] : mapping.entries()) {
        if (!key.sub_type.has_value() && builtin.count(key.type) > 0) {
            replaced.push_back(key.to_string());
        }
    }
    return replaced;
}

std::vector<Finding> validate_values(const CustomErrorMapping& mapping) {
    std::vector<Finding> findings;
    for (const auto& [key, entry] : mapping.entries()) {
        if (entry.v16.has_value() && entry.v16->info.has_value()) {
            validate_text(entry, entry.v16->info.value(), {"v16", "info"}, V16_INFO_MAX_LENGTH, findings);
        }
        if (entry.v2.has_value() && entry.v2->tech_info.has_value()) {
            validate_text(entry, entry.v2->tech_info.value(), {"v2", "techInfo"}, V2_TECH_INFO_MAX_LENGTH, findings);
        }
        if (entry.tier_mapping.has_value() && entry.tier_mapping->evse == 0 &&
            entry.tier_mapping->connector.has_value()) {
            findings.push_back(finding(Finding::Level::Error, entry, pointer(entry, {"tier_mapping"}),
                                       "evse 0 is the charging station and has no connector"));
        }
    }
    return findings;
}

std::vector<Finding> validate_topology(const CustomErrorMapping& mapping, const EvseTopology& topology) {
    std::vector<Finding> findings;
    for (const auto& [key, entry] : mapping.entries()) {
        if (!entry.tier_mapping.has_value() || entry.tier_mapping->evse == 0) {
            continue;
        }
        const auto evse = topology.find(entry.tier_mapping->evse);
        if (evse == topology.end()) {
            findings.push_back(finding(Finding::Level::Error, entry, pointer(entry, {"tier_mapping", "evse"}),
                                       "the charger has no EVSE " + std::to_string(entry.tier_mapping->evse)));
        } else if (entry.tier_mapping->connector.has_value() && entry.tier_mapping->connector.value() > evse->second) {
            findings.push_back(finding(Finding::Level::Error, entry, pointer(entry, {"tier_mapping", "connector"}),
                                       "EVSE " + std::to_string(evse->first) + " has no connector " +
                                           std::to_string(entry.tier_mapping->connector.value())));
        }
    }
    return findings;
}

std::vector<Finding> validate_device_model(const CustomErrorMapping& mapping, const DeviceModelLookupFunction& lookup,
                                           const EvseTopology& topology, bool strict) {
    std::vector<Finding> findings;
    for (const auto& [key, entry] : mapping.entries()) {
        if (!entry.v2.has_value() || !names_device_model_entry(entry.v2.value())) {
            continue;
        }
        const auto& v2 = entry.v2.value();
        ocpp::v2::Component component;
        component.name = v2.component_name.value_or(DEFAULT_COMPONENT_NAME);
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
        for (const auto& evse : candidate_evses(entry.tier_mapping, topology)) {
            component.evse = evse;
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

        const auto where =
            entry.tier_mapping.has_value() ? " on " + describe(entry.tier_mapping.value()) : std::string{};
        const auto message = component_known
                                 ? "the device model has no variable '" + variable.name.get() + "' on component '" +
                                       component.name.get() + "'" + where
                                 : "the device model has no component '" + component.name.get() + "'" + where;
        findings.push_back(
            finding(strict ? Finding::Level::Error : Finding::Level::Warning, entry, pointer(entry, {"v2"}), message));
    }
    return findings;
}

std::optional<std::string> mapping_override(const Entry& entry, const Everest::error::Error& error) {
    if (!entry.tier_mapping.has_value() || !error.origin.mapping.has_value() ||
        entry.tier_mapping.value() == error.origin.mapping.value()) {
        return std::nullopt;
    }
    return "entry '" + entry.key.to_string() + "' maps " + error.type + " of " + error.origin.module_id + "/" +
           error.origin.implementation_id + " to " + describe(entry.tier_mapping.value()) +
           ", overriding its module mapping " + describe(error.origin.mapping.value());
}

} // namespace ocpp_module_common::custom_error_mapping
