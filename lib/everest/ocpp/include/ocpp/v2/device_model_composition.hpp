// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <tuple>

#include <ocpp/v2/comparators.hpp>
#include <ocpp/v2/device_model_interface.hpp>
#include <ocpp/v2/device_model_registry_interface.hpp>
#include <ocpp/v2/device_model_storage_interface.hpp>

namespace ocpp {
namespace v2 {

/// \brief What a runtime owner registered, with the values of its variables
struct OwnerRegistration {
    std::string owner;
    std::shared_ptr<VariableValueProvider> provider;
    DeviceModelMap variables;
    /// \brief The last write issued before this registration: the writes up to it were forwarded to an earlier one
    WriteId registered_after_write{0};
    /// \brief Per attribute, the newest write that the value in memory reflects, stored or reported on
    std::map<std::tuple<Component, Variable, AttributeEnum>, WriteId> applied_writes;
};

/// \brief Where the values of a variable are kept. Every backing but OwnerStorage keeps a copy in memory, which
/// DeviceModel reads.
enum class ValueBacking {
    /// \brief The component tables of the storage, for the persistent attributes of the component config
    Storage,
    /// \brief The storage of a component config variable whose owner is not VARIABLE_OWNER_COMPONENT_CONFIG, the
    /// EVerest device model of the OCPP modules for example, which changes values without DeviceModel: every read and
    /// write goes there
    OwnerStorage,
    /// \brief The stored values of the storage, for the persistent attributes of a registered variable without a
    /// provider
    StoredValue,
    /// \brief The provider of the owner, which receives every write and keeps persistent values itself. The copy in
    /// memory changes when the provider accepts a write and when the owner reports a value
    Provider,
};

/// \brief A variable of the composed model and the layer that keeps it
struct ComposedVariable {
    VariableMetaData* meta_data{nullptr};
    /// \brief nullptr for a variable of the component config
    OwnerRegistration* registration{nullptr};
    ValueBacking backing{ValueBacking::Storage};
};

using ComposedModel = std::map<Component, std::map<Variable, ComposedVariable>>;

/// \brief Composes the model that the CSMS sees from the component config and the registrations, which define disjoint
/// variables. The result points into both arguments and is valid until one of them changes.
ComposedModel compose_device_model(DeviceModelMap& component_config,
                                   std::map<std::string, OwnerRegistration>& registrations);

/// \brief The stored values of persistent attributes of registered variables without a provider. They are kept by
/// component and variable identity, because the variables have no row in the component tables.
class StoredValues {
public:
    using ValueValidator = std::function<bool(const std::string& value)>;

    explicit StoredValues(DeviceModelStorageInterface& storage);

    /// \brief Applies the stored values to the persistent attributes of \p meta_data, which \p owner registers.
    /// Values that \p is_valid rejects are removed. A stored value belongs to the variable: a value stored under
    /// another owner applies as well and is recorded under \p owner.
    void restore(const std::string& owner, const Component& component, const Variable& variable,
                 VariableMetaData& meta_data, const ValueValidator& is_valid);

    SetVariableStatusEnum store(StoredVariableValue value);

    /// \brief Removes the values of \p variable on \p component, which \p owner registers with a provider that keeps
    /// its values
    void remove_of_provided_variable(const std::string& owner, const Component& component, const Variable& variable);

    /// \brief Removes the values of the variables for which \p registered returns false
    void remove_of_unregistered_variables(const std::function<bool(const Component&, const Variable&)>& registered);

private:
    using Key = std::tuple<Component, Variable, AttributeEnum>;

    /// \brief Removes the value of \p key from the storage, and from memory only if that succeeded
    void remove(Key key);

    DeviceModelStorageInterface& storage;
    std::map<Key, StoredVariableValue> values;
};

} // namespace v2
} // namespace ocpp
