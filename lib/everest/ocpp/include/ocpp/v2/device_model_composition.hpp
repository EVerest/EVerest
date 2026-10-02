// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <map>
#include <string>
#include <vector>

#include <ocpp/v2/comparators.hpp>
#include <ocpp/v2/device_model_interface.hpp>
#include <ocpp/v2/device_model_registry_interface.hpp>

namespace ocpp {
namespace v2 {

/// \brief What a runtime source registered, with the values of its variables
struct Registration {
    std::string source;
    VariableValueProvider* provider{nullptr};
    DeviceModelMap variables;
};

/// \brief A variable of the composed model and the layer that keeps it
struct ComposedVariable {
    VariableMetaData* meta_data{nullptr};
    /// \brief nullptr for a variable of the component config
    Registration* registration{nullptr};
};

using ComposedModel = std::map<Component, std::map<Variable, ComposedVariable>>;

struct Composition {
    ComposedModel model;
    /// \brief Variables of the component config left out because their EVSE or connector does not exist
    std::vector<std::pair<Component, Variable>> hidden;
};

/// \brief Composes the model that the CSMS sees. A registered variable replaces the variable of the same name from the
/// component config. Once a runtime source registered an EVSE or connector, variables of the component config on
/// EVSEs and connectors that no runtime source registered are left out. The result points into both arguments
/// and is valid until one of them changes.
Composition compose_device_model(DeviceModelMap& component_config, std::map<std::string, Registration>& registrations);

} // namespace v2
} // namespace ocpp
