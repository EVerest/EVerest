// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <ocpp/v16/variable_resolver.hpp>

#include <ocpp/v16/known_keys.hpp>
#include <ocpp/v2/comparators.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>

namespace {

bool matches(const ocpp::v2::ComponentVariable& cv, const ocpp::v2::Component& component,
             const ocpp::v2::Variable& variable) {
    return cv.variable.has_value() && (cv.component == component) && (cv.variable.value() == variable);
}

bool is_connection_config(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable) {
    using namespace ocpp::v2;
    if (component.name == "NetworkConfiguration") {
        return true; // any slot instance
    }
    if (matches(ControllerComponentVariables::NetworkConfigurationPriority, component, variable) ||
        matches(ControllerComponentVariables::ActiveNetworkProfile, component, variable)) {
        return true;
    }
    // SecurityCtrlr fallback CVs used by make_nc_kv_with_fallback for ChargePointId / AuthorizationKey
    return matches(ControllerComponentVariables::SecurityCtrlrIdentity, component, variable) ||
           matches(ControllerComponentVariables::BasicAuthPassword, component, variable);
}

bool is_read_only_derived(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable) {
    if (matches(ocpp::v2::ControllerComponentVariables::SupportedFeatureProfiles, component, variable)) {
        return true;
    }
    for (const auto& entry : ocpp::v16::keys::max_limit_entries) {
        if (entry.second != nullptr && matches(*entry.second, component, variable)) {
            return true;
        }
    }
    return false;
}

} // namespace

namespace ocpp::v16 {

VariableResolver::VariableResolver(ocpp::v2::Ocpp16CustomConfigMappings custom_mappings) :
    custom_mappings(std::move(custom_mappings)) {
    for (const auto& [key, cv] : this->custom_mappings) {
        auto [it, inserted] = custom_reverse.emplace(cv, ReverseResult{key, false});
        if (!inserted) {
            it->second.ambiguous = true; // two custom keys map to the same CV
        }
    }
}

std::optional<std::pair<ocpp::v2::Component, ocpp::v2::Variable>>
VariableResolver::key_to_cv(const std::string& key) const {
    // runtime order: known 1.6 keys first (never shadowed by custom mappings), then custom mappings
    if (const auto valid_key = keys::convert(key)) {
        // max-limit keys expose VariableCharacteristics.maxLimit, not a CV's Actual value
        if (keys::is_max_limit_key(valid_key.value())) {
            return std::nullopt;
        }
        return keys::convert_v2(valid_key.value());
    }
    if (const auto it = custom_mappings.find(key); it != custom_mappings.end()) {
        return it->second;
    }
    return std::nullopt;
}

ReverseResult VariableResolver::cv_to_key(const ocpp::v2::Component& component,
                                          const ocpp::v2::Variable& variable) const {
    ReverseResult result;
    const auto custom_it = custom_reverse.find({component, variable});
    if (auto standard = keys::convert_v2(component, variable, ocpp::v2::AttributeEnum::Actual)) {
        result.key = std::move(standard);
        // a custom mapping colliding with a standard reverse entry makes the CV ambiguous
        result.ambiguous = custom_it != custom_reverse.end();
        return result;
    }
    if (custom_it != custom_reverse.end()) {
        result = custom_it->second;
    }
    return result;
}

CVClass VariableResolver::classify(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable) const {
    if (is_connection_config(component, variable)) {
        return CVClass::ConnectionConfig;
    }
    if (is_read_only_derived(component, variable)) {
        return CVClass::ReadOnlyDerived;
    }
    const auto reverse = cv_to_key(component, variable);
    if (reverse.key.has_value() && !reverse.ambiguous) {
        return CVClass::KeyBacked;
    }
    return CVClass::Free;
}

} // namespace ocpp::v16
