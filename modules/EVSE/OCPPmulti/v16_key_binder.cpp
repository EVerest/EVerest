// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "v16_key_binder.hpp"

#include <algorithm>

#include <everest/logging.hpp>
#include <ocpp/v2/comparators.hpp>

namespace ocpp_multi {

namespace {

const ocpp::v2::RegisteredVariable* find_variable(const std::vector<ocpp::v2::RegisteredComponent>& components,
                                                  const ocpp::v2::Component& component,
                                                  const ocpp::v2::Variable& variable) {
    for (const auto& registered_component : components) {
        if (not(registered_component.component == component)) {
            continue;
        }
        for (const auto& registered_variable : registered_component.variables) {
            if (registered_variable.variable == variable) {
                return &registered_variable;
            }
        }
    }
    return nullptr;
}

bool has_actual_attribute(const ocpp::v2::RegisteredVariable& registered) {
    return std::any_of(registered.attributes.begin(), registered.attributes.end(), [](const auto& attribute) {
        return attribute.type.value_or(ocpp::v2::AttributeEnum::Actual) == ocpp::v2::AttributeEnum::Actual;
    });
}

} // namespace

V16KeyBinder::V16KeyBinder(ocpp::v16::VariableResolver& resolver, ocpp::v2::DeviceModelVariablesInterface& device_model,
                           ocpp::v2::Ocpp16KeyMappings file_mappings) :
    resolver(resolver), device_model(device_model), file_mappings(std::move(file_mappings)) {
    for (const auto& [key, target] : this->file_mappings) {
        this->file_key_by_target.emplace(target, key);
    }
}

std::optional<std::pair<ocpp::v2::RegistrationRejectionReason, std::string>>
V16KeyBinder::find_refusal(const std::string& key, const Target& target, const ocpp::v2::RegisteredVariable& registered,
                           const bool of_the_file) const {
    using ocpp::v2::RegistrationRejectionReason;
    const auto& [component, variable] = target;

    if (const auto it = this->file_key_by_target.find(target);
        not of_the_file and it != this->file_key_by_target.end()) {
        return std::make_pair(RegistrationRejectionReason::Ocpp16KeyInUse,
                              "the key mapping file maps the variable to key '" + it->second + "'");
    }
    if (not of_the_file and this->file_mappings.find(key) != this->file_mappings.end()) {
        return std::make_pair(RegistrationRejectionReason::Ocpp16KeyInUse,
                              std::string{"the key mapping file maps the key to another variable"});
    }
    if (const auto conflict = ocpp::v2::find_key_mapping_conflict(key, target, {}, {})) {
        return std::make_pair(conflict->in_use ? RegistrationRejectionReason::Ocpp16KeyInUse
                                               : RegistrationRejectionReason::Ocpp16KeyInvalid,
                              conflict->reason);
    }
    const auto lookup = [&](const ocpp::v2::Component& lookup_component,
                            const ocpp::v2::Variable& lookup_variable) -> std::optional<ocpp::v2::MappingTargetInfo> {
        if (lookup_component == component and lookup_variable == variable) {
            return ocpp::v2::MappingTargetInfo{registered.characteristics.dataType, has_actual_attribute(registered)};
        }
        const auto meta = this->device_model.get_variable_meta_data(lookup_component, lookup_variable);
        if (not meta.has_value()) {
            return std::nullopt;
        }
        const auto actual =
            this->device_model.get_mutability(lookup_component, lookup_variable, ocpp::v2::AttributeEnum::Actual);
        return ocpp::v2::MappingTargetInfo{meta->characteristics.dataType, actual.has_value()};
    };
    if (const auto problem = ocpp::v2::find_mapping_target_problem(key, target, lookup)) {
        return std::make_pair(RegistrationRejectionReason::Ocpp16KeyInvalid, "the variable is a target " + *problem);
    }
    return std::nullopt;
}

void V16KeyBinder::remove(Bound& bound) {
    for (const auto& [target, key] : bound) {
        this->resolver.remove_owner_key(key);
    }
    bound.clear();
}

std::vector<ocpp::v2::RejectedVariable>
V16KeyBinder::bind(const std::string& owner, const std::vector<ocpp::v2::RegisteredComponent>& components,
                   const std::vector<ocpp_module_common::device_model::DeclaredOcpp16Key>& keys) {
    std::vector<ocpp::v2::RejectedVariable> refused;
    auto handle = this->bound_by_owner.handle();
    auto& bound = (*handle)[owner];
    this->remove(bound);
    for (const auto& declared : keys) {
        const Target target{declared.component, declared.variable};
        const auto* registered = find_variable(components, declared.component, declared.variable);
        if (registered == nullptr) {
            continue;
        }
        const auto file_entry = this->file_mappings.find(declared.key);
        const bool of_the_file = file_entry != this->file_mappings.end() and file_entry->second == target;
        auto refusal = this->find_refusal(declared.key, target, *registered, of_the_file);
        // Left to the file before the configuration applied it, and while its entry is in effect. An entry dropped at
        // startup, because its target was not registered yet, stays dropped until the next start.
        if (of_the_file and not refusal.has_value() and
            (not this->file_mappings_loaded or this->resolver.key_to_cv(declared.key) == target)) {
            continue;
        }
        if (not refusal.has_value()) {
            if (const auto holder = this->resolver.add_owner_key(declared.key, declared.component, declared.variable)) {
                refusal = std::make_pair(ocpp::v2::RegistrationRejectionReason::Ocpp16KeyInUse, holder.value());
            }
        }
        if (refusal.has_value()) {
            EVLOG_warning << "OCPP 1.6 key " << declared.key << " of owner " << owner << " for "
                          << ocpp::v2::describe_mapping_target(declared.component, declared.variable)
                          << " refused: " << refusal->second;
            refused.push_back({declared.component, declared.variable, refusal->first, {}});
            continue;
        }
        bound.emplace(target, declared.key);
        EVLOG_info << "OCPP 1.6 key " << declared.key << " maps to "
                   << ocpp::v2::describe_mapping_target(declared.component, declared.variable) << " of owner " << owner;
    }
    if (bound.empty()) {
        handle->erase(owner);
    }
    return refused;
}

void V16KeyBinder::unbind_variables(const std::string& owner, const std::vector<ocpp::v2::RejectedVariable>& rejected) {
    auto handle = this->bound_by_owner.handle();
    const auto entry = handle->find(owner);
    if (entry == handle->end()) {
        return;
    }
    for (const auto& variable : rejected) {
        const auto it = entry->second.find(Target{variable.component, variable.variable});
        if (it != entry->second.end()) {
            this->resolver.remove_owner_key(it->second);
            entry->second.erase(it);
        }
    }
    if (entry->second.empty()) {
        handle->erase(entry);
    }
}

void V16KeyBinder::unbind(const std::string& owner) {
    auto handle = this->bound_by_owner.handle();
    const auto entry = handle->find(owner);
    if (entry == handle->end()) {
        return;
    }
    this->remove(entry->second);
    handle->erase(entry);
}

void V16KeyBinder::on_file_mappings_loaded() {
    this->file_mappings_loaded = true;
}

} // namespace ocpp_multi
