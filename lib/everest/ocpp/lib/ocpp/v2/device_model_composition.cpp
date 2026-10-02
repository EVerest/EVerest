// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <algorithm>
#include <set>

#include <everest/logging.hpp>
#include <ocpp/v2/device_model_composition.hpp>

namespace ocpp {
namespace v2 {

namespace {
/// \brief The EVSEs and connectors that sources other than OCPP_RUNTIME registered. libocpp registers variables on
/// EVSEs and connectors that exist already, so its registrations do not decide which exist.
class RegisteredStructure {
public:
    explicit RegisteredStructure(const std::map<std::string, Registration>& registrations) {
        for (const auto& [source, registration] : registrations) {
            if (source == VARIABLE_SOURCE_OCPP_RUNTIME) {
                continue;
            }
            for (const auto& [component, variables] : registration.variables) {
                if (!component.evse.has_value()) {
                    continue;
                }
                if (component.name == "EVSE") {
                    this->evse_ids.insert(component.evse->id);
                } else if (component.name == "Connector" and component.evse->connectorId.has_value()) {
                    this->connector_ids.insert({component.evse->id, component.evse->connectorId.value()});
                }
            }
        }
    }

    bool contains(const Component& component) const {
        if (this->evse_ids.empty() and this->connector_ids.empty()) {
            return true;
        }
        if (!component.evse.has_value()) {
            return true;
        }
        if (this->evse_ids.count(component.evse->id) == 0) {
            return false;
        }
        return !component.evse->connectorId.has_value() or
               this->connector_ids.count({component.evse->id, component.evse->connectorId.value()}) != 0;
    }

private:
    std::set<std::int32_t> evse_ids;
    std::set<std::pair<std::int32_t, std::int32_t>> connector_ids;
};
} // namespace

Composition compose_device_model(DeviceModelMap& component_config, std::map<std::string, Registration>& registrations) {
    Composition composition;
    for (auto& [source, registration] : registrations) {
        for (auto& [component, variables] : registration.variables) {
            for (auto& [variable, meta_data] : variables) {
                composition.model[component][variable] = ComposedVariable{&meta_data, &registration};
            }
        }
    }

    const RegisteredStructure structure{registrations};
    for (auto& [component, variables] : component_config) {
        const bool exists = structure.contains(component);
        for (auto& [variable, meta_data] : variables) {
            auto& composed_variables = composition.model[component];
            if (composed_variables.count(variable) != 0) {
                continue;
            }
            if (!exists) {
                composition.hidden.emplace_back(component, variable);
                continue;
            }
            composed_variables[variable] = ComposedVariable{&meta_data, nullptr};
        }
        if (composition.model[component].empty()) {
            composition.model.erase(component);
        }
    }
    return composition;
}

StoredValues::StoredValues(DeviceModelStorageInterface& storage) : storage{storage} {
    for (auto& stored : this->storage.get_stored_values()) {
        auto key = std::make_tuple(stored.component, stored.variable, stored.attribute);
        this->values.emplace(std::move(key), std::move(stored));
    }
}

void StoredValues::restore(const std::string& source, const Component& component, const Variable& variable,
                           VariableMetaData& meta_data, const ValueValidator& is_valid) {
    for (auto& attribute : meta_data.attributes) {
        const auto attribute_enum = attribute.type.value_or(AttributeEnum::Actual);
        const auto stored_it = this->values.find(std::make_tuple(component, variable, attribute_enum));
        if (stored_it == this->values.end()) {
            continue;
        }
        auto& stored = stored_it->second;
        if (!attribute.persistent.value_or(true) or !is_valid(stored.value)) {
            EVLOG_warning << "Discarding the stored value '" << stored.value << "' of " << component << "." << variable
                          << ": it does not fit the variable as source " << source << " registers it";
            this->storage.remove_stored_value(component, variable, attribute_enum);
            this->values.erase(stored_it);
            continue;
        }

        attribute.value = stored.value;
        if (stored.source != source) {
            stored.source = source;
            if (this->storage.set_stored_value(stored) != SetVariableStatusEnum::Accepted) {
                EVLOG_warning << "Could not record the source of the stored value of " << component << "." << variable;
            }
        }
    }
}

SetVariableStatusEnum StoredValues::store(StoredVariableValue value) {
    const auto result = this->storage.set_stored_value(value);
    if (result != SetVariableStatusEnum::Accepted) {
        return result;
    }
    auto key = std::make_tuple(value.component, value.variable, value.attribute);
    this->values[std::move(key)] = std::move(value);
    return result;
}

void StoredValues::remove_unregistered(const Registration& registration,
                                       const std::vector<RejectedVariable>& rejected) {
    for (auto it = this->values.begin(); it != this->values.end();) {
        const auto& [component, variable, attribute] = it->first;
        if (it->second.source != registration.source) {
            ++it;
            continue;
        }
        const auto component_it = registration.variables.find(component);
        if (component_it != registration.variables.end() and component_it->second.count(variable) != 0) {
            ++it;
            continue;
        }
        const bool is_rejected =
            std::any_of(rejected.begin(), rejected.end(),
                        [&component = component, &variable = variable](const RejectedVariable& rejected_variable) {
                            return rejected_variable.component == component and rejected_variable.variable == variable;
                        });
        if (is_rejected) {
            ++it;
            continue;
        }
        EVLOG_warning << "Removing the stored value of " << component << "." << variable << ": source "
                      << registration.source << " no longer registers the variable";
        this->storage.remove_stored_value(component, variable, attribute);
        it = this->values.erase(it);
    }
}

} // namespace v2
} // namespace ocpp
