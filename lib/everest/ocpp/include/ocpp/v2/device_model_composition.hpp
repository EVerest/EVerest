// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <map>
#include <string>
#include <tuple>

#include <ocpp/v2/comparators.hpp>
#include <ocpp/v2/device_model_interface.hpp>
#include <ocpp/v2/device_model_registry_interface.hpp>

namespace ocpp {
namespace v2 {

/// \brief What a runtime owner registered, with the values of its variables
struct OwnerRegistration {
    std::string owner;
    VariableValueProvider* provider{nullptr};
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
    /// \brief Memory only, for a registered variable without a provider: the owner registers its values at every start
    Memory,
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

} // namespace v2
} // namespace ocpp
