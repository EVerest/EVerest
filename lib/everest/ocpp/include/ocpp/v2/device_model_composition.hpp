// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include <ocpp/v2/comparators.hpp>
#include <ocpp/v2/device_model_interface.hpp>
#include <ocpp/v2/device_model_registry_interface.hpp>
#include <ocpp/v2/device_model_storage_interface.hpp>

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

/// \brief The stored values of persistent attributes of registered variables without a provider. They are kept by
/// component and variable identity, because the variables have no row in the component tables.
class StoredValues {
public:
    using ValueValidator = std::function<bool(const std::string& value)>;

    explicit StoredValues(DeviceModelStorageInterface& storage);

    /// \brief Applies the stored values to the persistent attributes of \p meta_data, which \p source registers.
    /// Values that \p is_valid rejects are removed. A stored value belongs to the variable: a value stored under
    /// another source applies as well and is recorded under \p source.
    void restore(const std::string& source, const Component& component, const Variable& variable,
                 VariableMetaData& meta_data, const ValueValidator& is_valid);

    SetVariableStatusEnum store(StoredVariableValue value);

    /// \brief Removes the values of \p source for the variables it no longer registers. A value of a variable in
    /// \p rejected is kept: the variable belongs to another owner, which may register it without a provider later.
    void remove_unregistered(const Registration& registration, const std::vector<RejectedVariable>& rejected);

private:
    using Key = std::tuple<Component, Variable, AttributeEnum>;

    DeviceModelStorageInterface& storage;
    std::map<Key, StoredVariableValue> values;
};

} // namespace v2
} // namespace ocpp
