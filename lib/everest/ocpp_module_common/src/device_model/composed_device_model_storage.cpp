// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/ocpp_module_common/device_model/composed_device_model_storage.hpp>
#include <everest/ocpp_module_common/device_model/definitions.hpp>

#include <stdexcept>

namespace ocpp_module_common::device_model {

bool ComposedDeviceModelStorage::register_device_model_storage(
    std::string device_model_storage_id, std::shared_ptr<ocpp::v2::DeviceModelStorageInterface> device_model_storage) {
    if (this->device_model_storages.find(device_model_storage_id) != this->device_model_storages.end()) {
        return false;
    }

    const auto device_model_map = device_model_storage->get_device_model();
    // store the owner of each variable to route requests to its device model storage
    for (const auto& [component, variable_map] : device_model_map) {
        for (const auto& [variable, variable_meta] : variable_map) {
            const auto owner = variable_meta.owner.value_or(VARIABLE_OWNER_OCPP);

            // On overlap, the storage registered last takes precedence. The integrator is responsible for
            // avoiding overlaps.
            const auto component_it = this->component_variable_owner_map.find(component);
            if (component_it != this->component_variable_owner_map.end() &&
                component_it->second.find(variable) != component_it->second.end()) {
                EVLOG_warning << "Variable " << variable.name << " of component " << component.name
                              << " is defined in multiple device model storages; owner " << owner
                              << " takes precedence. The integrator is responsible for avoiding overlapping "
                                 "device model definitions.";
            }

            this->component_variable_owner_map[component][variable] = owner;
        }
    }

    this->device_model_storages[device_model_storage_id] = device_model_storage;
    return true;
}

ocpp::v2::DeviceModelMap ComposedDeviceModelStorage::get_device_model() {
    ocpp::v2::DeviceModelMap device_model_map;
    // Merge per variable, not per component: each variable is contributed only by the storage that owns it
    // according to the owner map, so the merged model is consistent with request routing when storages
    // define overlapping components or variables.
    for (const auto& [storage_id, device_model_storage] : this->device_model_storages) {
        for (auto& [component, variable_map] : device_model_storage->get_device_model()) {
            for (auto& [variable, variable_meta] : variable_map) {
                if (get_variable_owner(component, variable) == storage_id) {
                    device_model_map[component].insert_or_assign(variable, std::move(variable_meta));
                }
            }
        }
    }
    return device_model_map;
}

std::optional<ocpp::v2::VariableAttribute>
ComposedDeviceModelStorage::get_variable_attribute(const ocpp::v2::Component& component_id,
                                                   const ocpp::v2::Variable& variable_id,
                                                   const ocpp::v2::AttributeEnum& attribute_enum) {
    const auto variable_owner = get_variable_owner(component_id, variable_id);
    if (this->device_model_storages.find(variable_owner) == this->device_model_storages.end()) {
        return std::nullopt;
    }
    return this->device_model_storages.at(variable_owner)
        ->get_variable_attribute(component_id, variable_id, attribute_enum);
}

std::vector<ocpp::v2::VariableAttribute>
ComposedDeviceModelStorage::get_variable_attributes(const ocpp::v2::Component& component_id,
                                                    const ocpp::v2::Variable& variable_id,
                                                    const std::optional<ocpp::v2::AttributeEnum>& attribute_enum) {
    const auto variable_owner = get_variable_owner(component_id, variable_id);
    if (this->device_model_storages.find(variable_owner) == this->device_model_storages.end()) {
        return {};
    }
    return this->device_model_storages.at(variable_owner)
        ->get_variable_attributes(component_id, variable_id, attribute_enum);
}

ocpp::v2::SetVariableStatusEnum ComposedDeviceModelStorage::set_variable_attribute_value(
    const ocpp::v2::Component& component_id, const ocpp::v2::Variable& variable_id,
    const ocpp::v2::AttributeEnum& attribute_enum, const std::string& value, const std::string& source) {
    const auto variable_owner = get_variable_owner(component_id, variable_id);
    if (this->device_model_storages.find(variable_owner) == this->device_model_storages.end()) {
        return ocpp::v2::SetVariableStatusEnum::Rejected;
    }
    return this->device_model_storages.at(variable_owner)
        ->set_variable_attribute_value(component_id, variable_id, attribute_enum, value, source);
}

std::optional<ocpp::v2::VariableMonitoringMeta>
ComposedDeviceModelStorage::set_monitoring_data(const ocpp::v2::SetMonitoringData& data,
                                                const ocpp::v2::VariableMonitorType type) {
    auto* storage = this->ocpp_storage();
    if (storage == nullptr) {
        return std::nullopt;
    }
    return storage->set_monitoring_data(data, type);
}

bool ComposedDeviceModelStorage::update_monitoring_reference(const int32_t monitor_id,
                                                             const std::string& reference_value) {
    auto* storage = this->ocpp_storage();
    if (storage == nullptr) {
        return false;
    }
    return storage->update_monitoring_reference(monitor_id, reference_value);
}

ocpp::v2::ClearMonitoringStatusEnum ComposedDeviceModelStorage::clear_variable_monitor(int monitor_id,
                                                                                       bool allow_protected) {
    auto* storage = this->ocpp_storage();
    if (storage == nullptr) {
        return ocpp::v2::ClearMonitoringStatusEnum::Rejected;
    }
    return storage->clear_variable_monitor(monitor_id, allow_protected);
}

int32_t ComposedDeviceModelStorage::clear_custom_variable_monitors() {
    auto* storage = this->ocpp_storage();
    if (storage == nullptr) {
        return 0;
    }
    return storage->clear_custom_variable_monitors();
}

// Monitors and stored values are keyed by name and need no row in the component tables, so they all live in
// the OCPP storage, whichever storage holds the variable.
std::vector<ocpp::v2::StoredMonitor> ComposedDeviceModelStorage::get_all_monitors() {
    auto* storage = this->ocpp_storage();
    if (storage == nullptr) {
        return {};
    }
    return storage->get_all_monitors();
}

std::vector<ocpp::v2::StoredVariableValue> ComposedDeviceModelStorage::get_stored_values() {
    auto* storage = this->ocpp_storage();
    if (storage == nullptr) {
        return {};
    }
    return storage->get_stored_values();
}

ocpp::v2::SetVariableStatusEnum
ComposedDeviceModelStorage::set_stored_value(const ocpp::v2::StoredVariableValue& value) {
    auto* storage = this->ocpp_storage();
    if (storage == nullptr) {
        return ocpp::v2::SetVariableStatusEnum::Rejected;
    }
    return storage->set_stored_value(value);
}

bool ComposedDeviceModelStorage::remove_stored_value(const ocpp::v2::Component& component,
                                                     const ocpp::v2::Variable& variable,
                                                     const ocpp::v2::AttributeEnum& attribute_enum) {
    auto* storage = this->ocpp_storage();
    if (storage == nullptr) {
        return false;
    }
    return storage->remove_stored_value(component, variable, attribute_enum);
}

void ComposedDeviceModelStorage::check_integrity() {
    for (const auto& [name, device_model_storage] : this->device_model_storages) {
        device_model_storage->check_integrity();
    }
}

bool ComposedDeviceModelStorage::create_network_configuration_slot_from_default_schema(std::int32_t new_slot) {
    // NetworkConfiguration_<N> components live in the OCPP storage (the SQLite-backed
    // EverestDeviceModelStorage). Without this dispatch, libocpp's blob-migration fallback hits
    // DeviceModelStorageInterface's default virtual (returns false) and the operator's
    // NetworkConnectionProfiles blob ends up cleared without ever populating the per-slot
    // device-model rows on targets that ship no NetworkConfiguration_<N>.json.
    auto* storage = this->ocpp_storage();
    if (storage == nullptr) {
        return false;
    }
    return storage->create_network_configuration_slot_from_default_schema(new_slot);
}

ocpp::v2::DeviceModelStorageInterface* ComposedDeviceModelStorage::ocpp_storage() {
    const auto it = this->device_model_storages.find(VARIABLE_OWNER_OCPP);
    if (it == this->device_model_storages.end()) {
        EVLOG_error << "No OCPP device model storage registered";
        return nullptr;
    }
    return it->second.get();
}

std::string
ocpp_module_common::device_model::ComposedDeviceModelStorage::get_variable_owner(const ocpp::v2::Component& component,
                                                                                 const ocpp::v2::Variable& variable) {
    if (this->component_variable_owner_map.find(component) == this->component_variable_owner_map.end()) {
        return VARIABLE_OWNER_OCPP;
    }
    const auto& variable_map = this->component_variable_owner_map.at(component);
    if (variable_map.find(variable) == variable_map.end()) {
        return VARIABLE_OWNER_OCPP;
    }
    return variable_map.at(variable);
}

std::unique_ptr<ComposedDeviceModelStorage>
make_composed_device_model_storage(std::shared_ptr<ocpp::v2::DeviceModelStorageInterface> ocpp_storage,
                                   std::shared_ptr<ocpp::v2::DeviceModelStorageInterface> everest_storage) {
    if (ocpp_storage == nullptr) {
        throw std::invalid_argument("Cannot compose device model storage: the OCPP device model storage is null");
    }

    auto composed_device_model_storage = std::make_unique<ComposedDeviceModelStorage>();
    // note - registration snapshots get_device_model(), which causes a slight delay; scope for performance tuning
    composed_device_model_storage->register_device_model_storage(VARIABLE_OWNER_OCPP, std::move(ocpp_storage));
    if (everest_storage != nullptr) {
        composed_device_model_storage->register_device_model_storage(VARIABLE_OWNER_EVEREST,
                                                                     std::move(everest_storage));
    } else {
        EVLOG_warning << "No EVerest device model storage provided; composed device model contains only the "
                         "OCPP storage";
    }
    return composed_device_model_storage;
}

} // namespace ocpp_module_common::device_model
