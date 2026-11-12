// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v16/variable_resolver.hpp>

#include <everest/logging.hpp>
#include <ocpp/v16/known_keys.hpp>
#include <ocpp/v2/comparators.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>

#include <algorithm>
#include <array>

namespace {

// Values the OCPP 1.6 stack derives (NumberOfConnectors, SupportedFeatureProfiles) or expands into several keys
// (MeterPublicKeys, DefaultPriceText) at fixed device model locations
constexpr std::array not_overridable{
    ocpp::v16::keys::valid_keys::NumberOfConnectors,
    ocpp::v16::keys::valid_keys::SupportedFeatureProfiles,
    ocpp::v16::keys::valid_keys::MeterPublicKeys,
    ocpp::v16::keys::valid_keys::DefaultPriceText,
};

bool matches(const ocpp::v2::ComponentVariable& cv, const ocpp::v2::Component& component,
             const ocpp::v2::Variable& variable) {
    return cv.variable.has_value() && (cv.component == component) && (cv.variable.value() == variable);
}

} // namespace

namespace ocpp::v16 {

bool is_connection_config_cv(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable) {
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

bool is_read_only_derived_cv(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable) {
    if (matches(ocpp::v2::ControllerComponentVariables::SupportedFeatureProfiles, component, variable)) {
        return true;
    }
    for (const auto& entry : keys::max_limit_entries) {
        if (entry.second != nullptr && matches(*entry.second, component, variable)) {
            return true;
        }
    }
    return false;
}

// TODO(dynamic-runtime-dm-updates): check whether other keys must be overridable once application modules provide
// device model variables
bool is_overridable_key(keys::valid_keys key) {
    if (std::find(not_overridable.begin(), not_overridable.end(), key) != not_overridable.end()) {
        return false;
    }
    const auto builtin = keys::convert_v2(key);
    return builtin.has_value() && builtin->first == ocpp::v2::ControllerComponents::OCPP16LegacyCtrlr;
}

VariableResolver::VariableResolver(const ocpp::v2::Ocpp16KeyMappings& custom_mappings) {
    // the key that already owns a CV, so that every CV maps back to at most one key
    const auto owner_of =
        [this](const std::string& name,
               const std::pair<ocpp::v2::Component, ocpp::v2::Variable>& cv) -> std::optional<std::string> {
        if (auto builtin = keys::convert_v2(cv.first, cv.second, ocpp::v2::AttributeEnum::Actual);
            builtin.has_value() && builtin.value() != name) {
            return builtin;
        }
        if (const auto it = overrides_by_cv.find(cv); it != overrides_by_cv.end()) {
            return std::string{keys::convert(it->second)};
        }
        if (const auto it = custom_reverse.find(cv); it != custom_reverse.end()) {
            return it->second;
        }
        return std::nullopt;
    };

    for (const auto& [name, cv] : custom_mappings) {
        const auto key = keys::convert(name);
        if (key.has_value() && !is_overridable_key(key.value())) {
            EVLOG_warning << "OCPP 1.6 key " << name << " cannot be custom-mapped, ignoring the mapping to "
                          << ocpp::v2::describe_mapping_target(cv.first, cv.second);
            continue;
        }
        if (const auto owner = owner_of(name, cv)) {
            EVLOG_error << "OCPP 1.6 key " << name << " is custom-mapped to "
                        << ocpp::v2::describe_mapping_target(cv.first, cv.second) << ", which is the target of key '"
                        << owner.value() << "'; ignoring the mapping";
            continue;
        }
        if (!key.has_value()) {
            this->custom_mappings.emplace(name, cv);
            custom_reverse.emplace(cv, name);
            continue;
        }
        overrides_by_key.emplace(key.value(), cv);
        overrides_by_cv.emplace(cv, key.value());
    }
}

std::optional<std::pair<ocpp::v2::Component, ocpp::v2::Variable>>
VariableResolver::key_to_cv(const std::string& key) const {
    // runtime order: known 1.6 keys first (built-in mapping or its override), then custom mappings
    if (const auto valid_key = keys::convert(key)) {
        // max-limit keys expose VariableCharacteristics.maxLimit, not a CV's Actual value
        if (keys::is_max_limit_key(valid_key.value())) {
            return std::nullopt;
        }
        return known_key_to_cv(valid_key.value());
    }
    if (const auto it = custom_mappings.find(key); it != custom_mappings.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<std::string> VariableResolver::cv_to_key(const ocpp::v2::Component& component,
                                                       const ocpp::v2::Variable& variable) const {
    if (auto standard = cv_to_known_key(component, variable)) {
        return standard;
    }
    if (const auto it = custom_reverse.find({component, variable}); it != custom_reverse.end()) {
        return it->second;
    }
    return std::nullopt;
}

keys::DeviceModel_CV VariableResolver::known_key_to_cv(keys::valid_keys key) const {
    if (const auto it = overrides_by_key.find(key); it != overrides_by_key.end()) {
        return it->second;
    }
    return keys::convert_v2(key);
}

std::optional<std::string> VariableResolver::cv_to_known_key(const ocpp::v2::Component& component,
                                                             const ocpp::v2::Variable& variable,
                                                             ocpp::v2::AttributeEnum attribute) const {
    if (attribute == ocpp::v2::AttributeEnum::Actual) {
        if (const auto it = overrides_by_cv.find({component, variable}); it != overrides_by_cv.end()) {
            return std::string{keys::convert(it->second)};
        }
    }
    auto builtin = keys::convert_v2(component, variable, attribute);
    if (builtin.has_value()) {
        if (const auto key = keys::convert(builtin.value()); key.has_value() && is_overridden(key.value())) {
            return std::nullopt;
        }
    }
    return builtin;
}

bool VariableResolver::is_overridden(keys::valid_keys key) const {
    return overrides_by_key.find(key) != overrides_by_key.end();
}

CVClass VariableResolver::classify(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable) const {
    if (is_connection_config_cv(component, variable)) {
        return CVClass::ConnectionConfig;
    }
    if (is_read_only_derived_cv(component, variable)) {
        return CVClass::ReadOnlyDerived;
    }
    if (cv_to_key(component, variable).has_value()) {
        return CVClass::KeyBacked;
    }
    return CVClass::Free;
}

} // namespace ocpp::v16
