// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v2/device_model_composition.hpp>

namespace ocpp {
namespace v2 {

ComposedModel compose_device_model(DeviceModelMap& component_config,
                                   std::map<std::string, OwnerRegistration>& registrations) {
    ComposedModel model;
    for (auto& [owner, registration] : registrations) {
        for (auto& [component, variables] : registration.variables) {
            for (auto& [variable, meta_data] : variables) {
                const auto backing = registration.provider != nullptr ? ValueBacking::Provider : ValueBacking::Memory;
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

} // namespace v2
} // namespace ocpp
