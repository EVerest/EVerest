// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v2/availability_state_registration.hpp>

#include <ocpp/v2/ctrlr_component_variables.hpp>

#include <utility>

namespace ocpp::v2 {

namespace {
RegisteredComponent make_availability_state(const ComponentVariable& component_variable) {
    RegisteredVariable registered;
    registered.variable = component_variable.variable.value();
    registered.characteristics.dataType = DataEnum::OptionList;
    registered.characteristics.valuesList = "Available,Occupied,Reserved,Unavailable,Faulted";
    registered.characteristics.supportsMonitoring = true;
    VariableAttribute attribute;
    attribute.type = AttributeEnum::Actual;
    attribute.mutability = MutabilityEnum::ReadOnly;
    attribute.persistent = false;
    attribute.value = "Available";
    registered.attributes.push_back(attribute);
    return {component_variable.component, {registered}};
}
} // namespace

std::size_t register_availability_states(DeviceModel& device_model,
                                         const std::map<std::int32_t, std::int32_t>& evse_connector_structure) {
    std::vector<ComponentVariable> availability_states{ControllerComponentVariables::ChargingStationAvailabilityState};
    for (const auto& [evse_id, number_of_connectors] : evse_connector_structure) {
        availability_states.push_back(
            EvseComponentVariables::get_component_variable(evse_id, EvseComponentVariables::AvailabilityState));
        for (std::int32_t connector_id = 1; connector_id <= number_of_connectors; connector_id++) {
            availability_states.push_back(ConnectorComponentVariables::get_component_variable(
                evse_id, connector_id, ConnectorComponentVariables::AvailabilityState));
        }
    }

    std::vector<RegisteredComponent> components;
    for (const auto& availability_state : availability_states) {
        const auto meta_data =
            device_model.get_variable_meta_data(availability_state.component, availability_state.variable.value());
        if (meta_data.has_value() and meta_data->owner != VARIABLE_OWNER_OCPP_STACK) {
            continue;
        }
        components.push_back(make_availability_state(availability_state));
    }

    const auto result = device_model.register_components_of(VARIABLE_OWNER_OCPP_STACK, components, nullptr);
    return components.size() - result.rejected.size();
}

std::shared_ptr<DeviceModel>
create_device_model_with_availability_states(const std::map<std::int32_t, std::int32_t>& evse_connector_structure,
                                             std::unique_ptr<DeviceModelStorageInterface> storage) {
    auto device_model = std::make_shared<DeviceModel>(std::move(storage));
    register_availability_states(*device_model, evse_connector_structure);
    return device_model;
}

} // namespace ocpp::v2
