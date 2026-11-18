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

bool VariableResolver::Layer::holds_key(const std::string& key) const {
    if (const auto known = keys::convert(key)) {
        return overrides_by_key.find(known.value()) != overrides_by_key.end();
    }
    return custom.find(key) != custom.end();
}

std::optional<std::string>
VariableResolver::Layer::key_of(const std::pair<ocpp::v2::Component, ocpp::v2::Variable>& cv) const {
    if (const auto it = overrides_by_cv.find(cv); it != overrides_by_cv.end()) {
        return std::string{keys::convert(it->second)};
    }
    if (const auto it = custom_reverse.find(cv); it != custom_reverse.end()) {
        return it->second;
    }
    return std::nullopt;
}

void VariableResolver::Layer::add(const std::string& key,
                                  const std::pair<ocpp::v2::Component, ocpp::v2::Variable>& cv) {
    if (const auto known = keys::convert(key)) {
        overrides_by_key.emplace(known.value(), cv);
        overrides_by_cv.emplace(cv, known.value());
        return;
    }
    custom.emplace(key, cv);
    custom_reverse.emplace(cv, key);
}

std::optional<std::string>
VariableResolver::holder_of(const std::string& key,
                            const std::pair<ocpp::v2::Component, ocpp::v2::Variable>& cv) const {
    if (auto builtin = keys::convert_v2(cv.first, cv.second, ocpp::v2::AttributeEnum::Actual);
        builtin.has_value() && builtin.value() != key) {
        return builtin;
    }
    if (auto holder = file.key_of(cv)) {
        return holder;
    }
    return owner.key_of(cv);
}

VariableResolver::Layer VariableResolver::make_file_layer(const ocpp::v2::Ocpp16KeyMappings& custom_mappings) const {
    Layer layer;
    for (const auto& [name, cv] : custom_mappings) {
        const auto key = keys::convert(name);
        if (key.has_value() && !is_overridable_key(key.value())) {
            EVLOG_warning << "OCPP 1.6 key " << name << " cannot be custom-mapped, ignoring the mapping to "
                          << ocpp::v2::describe_mapping_target(cv.first, cv.second);
            continue;
        }
        if (owner.holds_key(name)) {
            EVLOG_error << "OCPP 1.6 key " << name << " is custom-mapped to "
                        << ocpp::v2::describe_mapping_target(cv.first, cv.second)
                        << ", but a device model owner holds the key; ignoring the mapping";
            continue;
        }
        auto holder = layer.key_of(cv);
        if (!holder.has_value()) {
            holder = holder_of(name, cv);
        }
        if (holder.has_value() && holder.value() != name) {
            EVLOG_error << "OCPP 1.6 key " << name << " is custom-mapped to "
                        << ocpp::v2::describe_mapping_target(cv.first, cv.second) << ", which is the target of key '"
                        << holder.value() << "'; ignoring the mapping";
            continue;
        }
        layer.add(name, cv);
    }
    return layer;
}

VariableResolver::VariableResolver(const ocpp::v2::Ocpp16KeyMappings& custom_mappings) {
    file = make_file_layer(custom_mappings);
}

void VariableResolver::replace_file_mappings(const ocpp::v2::Ocpp16KeyMappings& custom_mappings) {
    std::unique_lock lock(mutex);
    // the new layer is checked against the owner layer only, never against the file layer it replaces
    file = Layer{};
    file = make_file_layer(custom_mappings);
}

std::optional<std::string> VariableResolver::add_owner_key(const std::string& key, const ocpp::v2::Component& component,
                                                           const ocpp::v2::Variable& variable) {
    const auto cv = std::make_pair(component, variable);
    std::unique_lock lock(mutex);
    if (const auto known = keys::convert(key); known.has_value() && !is_overridable_key(known.value())) {
        return "OCPP 1.6 key " + key + " cannot be custom-mapped";
    }
    if (file.holds_key(key) || owner.holds_key(key)) {
        return "OCPP 1.6 key " + key + " is mapped already";
    }
    if (const auto holder = holder_of(key, cv)) {
        return ocpp::v2::describe_mapping_target(component, variable) + " is the target of OCPP 1.6 key '" +
               holder.value() + "'";
    }
    owner.add(key, cv);
    return std::nullopt;
}

void VariableResolver::remove_owner_key(const std::string& key) {
    std::unique_lock lock(mutex);
    if (const auto known = keys::convert(key)) {
        if (const auto it = owner.overrides_by_key.find(known.value()); it != owner.overrides_by_key.end()) {
            owner.overrides_by_cv.erase(it->second);
            owner.overrides_by_key.erase(it);
        }
        return;
    }
    if (const auto it = owner.custom.find(key); it != owner.custom.end()) {
        owner.custom_reverse.erase(it->second);
        owner.custom.erase(it);
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
    return custom_key_to_cv(key);
}

std::optional<std::pair<ocpp::v2::Component, ocpp::v2::Variable>>
VariableResolver::custom_key_to_cv(const std::string& key) const {
    std::shared_lock lock(mutex);
    if (const auto it = file.custom.find(key); it != file.custom.end()) {
        return it->second;
    }
    if (const auto it = owner.custom.find(key); it != owner.custom.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<std::string> VariableResolver::cv_to_key(const ocpp::v2::Component& component,
                                                       const ocpp::v2::Variable& variable) const {
    std::shared_lock lock(mutex);
    return cv_to_key_unlocked(component, variable);
}

std::optional<std::string> VariableResolver::cv_to_key_unlocked(const ocpp::v2::Component& component,
                                                                const ocpp::v2::Variable& variable) const {
    if (auto standard = cv_to_known_key_unlocked(component, variable, ocpp::v2::AttributeEnum::Actual)) {
        return standard;
    }
    const auto cv = std::make_pair(component, variable);
    if (const auto it = file.custom_reverse.find(cv); it != file.custom_reverse.end()) {
        return it->second;
    }
    if (const auto it = owner.custom_reverse.find(cv); it != owner.custom_reverse.end()) {
        return it->second;
    }
    return std::nullopt;
}

keys::DeviceModel_CV VariableResolver::known_key_to_cv(keys::valid_keys key) const {
    std::shared_lock lock(mutex);
    return known_key_to_cv_unlocked(key);
}

keys::DeviceModel_CV VariableResolver::known_key_to_cv_unlocked(keys::valid_keys key) const {
    if (const auto it = file.overrides_by_key.find(key); it != file.overrides_by_key.end()) {
        return it->second;
    }
    if (const auto it = owner.overrides_by_key.find(key); it != owner.overrides_by_key.end()) {
        return it->second;
    }
    return keys::convert_v2(key);
}

std::optional<std::string> VariableResolver::cv_to_known_key(const ocpp::v2::Component& component,
                                                             const ocpp::v2::Variable& variable,
                                                             ocpp::v2::AttributeEnum attribute) const {
    std::shared_lock lock(mutex);
    return cv_to_known_key_unlocked(component, variable, attribute);
}

std::optional<std::string> VariableResolver::cv_to_known_key_unlocked(const ocpp::v2::Component& component,
                                                                      const ocpp::v2::Variable& variable,
                                                                      ocpp::v2::AttributeEnum attribute) const {
    if (attribute == ocpp::v2::AttributeEnum::Actual) {
        const auto cv = std::make_pair(component, variable);
        for (const auto* layer : {&file, &owner}) {
            if (const auto it = layer->overrides_by_cv.find(cv); it != layer->overrides_by_cv.end()) {
                return std::string{keys::convert(it->second)};
            }
        }
    }
    auto builtin = keys::convert_v2(component, variable, attribute);
    if (builtin.has_value()) {
        if (const auto key = keys::convert(builtin.value()); key.has_value() && is_overridden_unlocked(key.value())) {
            return std::nullopt;
        }
    }
    return builtin;
}

bool VariableResolver::is_overridden(keys::valid_keys key) const {
    std::shared_lock lock(mutex);
    return is_overridden_unlocked(key);
}

bool VariableResolver::is_overridden_unlocked(keys::valid_keys key) const {
    return file.overrides_by_key.find(key) != file.overrides_by_key.end() ||
           owner.overrides_by_key.find(key) != owner.overrides_by_key.end();
}

std::map<keys::valid_keys, std::pair<ocpp::v2::Component, ocpp::v2::Variable>> VariableResolver::overrides() const {
    std::shared_lock lock(mutex);
    auto result = file.overrides_by_key;
    result.insert(owner.overrides_by_key.begin(), owner.overrides_by_key.end());
    return result;
}

ocpp::v2::Ocpp16KeyMappings VariableResolver::custom_keys() const {
    std::shared_lock lock(mutex);
    auto result = file.custom;
    result.insert(owner.custom.begin(), owner.custom.end());
    return result;
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
