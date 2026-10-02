// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <set>

#include <ocpp/v2/device_model_composition.hpp>

namespace ocpp {
namespace v2 {

namespace {
/// \brief The EVSEs and connectors that runtime sources registered
class RegisteredStructure {
public:
    explicit RegisteredStructure(const std::map<std::string, Registration>& registrations) {
        for (const auto& [source, registration] : registrations) {
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

} // namespace v2
} // namespace ocpp
