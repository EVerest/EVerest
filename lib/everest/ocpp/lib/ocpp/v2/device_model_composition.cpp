// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <vector>

#include <everest/database/exceptions.hpp>
#include <everest/logging.hpp>
#include <ocpp/v2/device_model_composition.hpp>

namespace ocpp {
namespace v2 {

ComposedModel compose_device_model(DeviceModelMap& component_config,
                                   std::map<std::string, OwnerRegistration>& registrations) {
    ComposedModel model;
    for (auto& [owner, registration] : registrations) {
        for (auto& [component, variables] : registration.variables) {
            for (auto& [variable, meta_data] : variables) {
                const auto backing =
                    registration.provider != nullptr ? ValueBacking::Provider : ValueBacking::StoredValue;
                model[component][variable] = ComposedVariable{&meta_data, &registration, backing};
            }
        }
    }
    for (auto& [component, variables] : component_config) {
        for (auto& [variable, meta_data] : variables) {
            const auto backing =
                meta_data.owner == VARIABLE_OWNER_COMPONENT_CONFIG ? ValueBacking::Storage : ValueBacking::OwnerStorage;
            model[component].try_emplace(variable, ComposedVariable{&meta_data, nullptr, backing});
        }
    }
    return model;
}

StoredValues::StoredValues(DeviceModelStorageInterface& storage) : storage{storage} {
    for (auto& stored : this->storage.get_stored_values()) {
        auto key = std::make_tuple(stored.component, stored.variable, stored.attribute);
        this->values.emplace(std::move(key), std::move(stored));
    }
}

void StoredValues::restore(const std::string& owner, const Component& component, const Variable& variable,
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
                          << ": it does not fit the variable as owner " << owner << " registers it";
            this->remove(stored_it->first);
            continue;
        }

        attribute.value = stored.value;
        if (stored.owner != owner) {
            stored.owner = owner;
            if (this->storage.set_stored_value(stored) != SetVariableStatusEnum::Accepted) {
                EVLOG_warning << "Could not record the owner of the stored value of " << component << "." << variable;
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

void StoredValues::remove_of_unregistered_variables(
    const std::function<bool(const Component&, const Variable&)>& registered) {
    std::vector<Key> unregistered;
    for (const auto& [key, value] : this->values) {
        if (not registered(value.component, value.variable)) {
            unregistered.push_back(key);
        }
    }
    for (const auto& key : unregistered) {
        const auto& [component, variable, attribute] = key;
        EVLOG_warning << "Removing the stored value of " << component << "." << variable
                      << ": no owner registers the variable";
        this->remove(key);
    }
}

void StoredValues::remove(Key key) {
    const auto& [component, variable, attribute] = key;
    try {
        if (not this->storage.remove_stored_value(component, variable, attribute)) {
            EVLOG_warning << "Could not remove the stored value of " << component << "." << variable
                          << " from the storage";
            return;
        }
    } catch (const everest::db::Exception& e) {
        EVLOG_error << "Could not remove the stored value of " << component << "." << variable
                    << " from the storage: " << e.what();
        return;
    }
    this->values.erase(key);
}

} // namespace v2
} // namespace ocpp
