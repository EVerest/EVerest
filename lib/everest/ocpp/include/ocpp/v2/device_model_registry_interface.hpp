// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <ocpp/v2/device_model_interface.hpp>
#include <ocpp/v2/device_model_storage_interface.hpp>
#include <ocpp/v2/ocpp_types.hpp>

namespace ocpp {
namespace v2 {

/// \brief Identifies a write that DeviceModel forwards to a provider. Write ids increase in the order in which
/// DeviceModel issues the writes, starting at 1.
using WriteId = std::uint64_t;

/// \brief Receives the writes to the variables of a runtime owner
class VariableValueProvider {
public:
    virtual ~VariableValueProvider() = default;

    /// \brief Applies \p value to \p attribute of \p variable on \p component at the owner. Called without the
    /// DeviceModel lock on the thread of the caller, a SetVariables.req for example, which waits for it: DeviceModel
    /// sets no deadline, so a provider answers in time itself, well within the message timeout of the CSMS, and
    /// rejects a write it cannot complete by then. Calls are not serialised, also not for one variable.
    ///
    /// Accepted and RebootRequired mean that the owner applied exactly \p value: DeviceModel then stores it in memory,
    /// so the owner does not report it. An owner that would apply another value, a clamped one for example, rejects
    /// the write. When writes to one variable overlap, the model keeps the value of the one with the highest
    /// \p write_id, so an owner applies them in that order too.
    /// \param source The source of the value (e.g. 'csms', 'internal')
    /// \param write_id The id of this write, which the owner passes to notify_value_changed, see there
    virtual SetVariableStatusEnum set_value(const Component& component, const Variable& variable,
                                            AttributeEnum attribute, const std::string& value,
                                            const std::string& source, WriteId write_id) = 0;
};

/// \brief A variable that an owner registers at runtime
struct RegisteredVariable {
    Variable variable;
    VariableCharacteristics characteristics;
    /// \brief The attributes with their initial values. An attribute without \c persistent is not persistent, one
    /// without \c type is the Actual attribute. A persistent attribute is kept across restarts by the owner: it is
    /// reported as persistent, but DeviceModel does not store its value.
    std::vector<VariableAttribute> attributes;
};

/// \brief A component with the variables that an owner registers for it
struct RegisteredComponent {
    Component component;
    std::vector<RegisteredVariable> variables;
};

enum class RegistrationRejectionReason {
    /// \brief The component config or another runtime owner defines the variable already
    AlreadyDefined,
    /// \brief The variable has no attributes
    NoAttributes,
    /// \brief The owner is VARIABLE_OWNER_COMPONENT_CONFIG, which only libocpp uses
    ReservedOwner,
    /// \brief The component or the variable has an empty instance, or the component an EVSE or connector id of 0.
    /// Monitors and stored values key an absent instance as '' and an absent id as 0, so they could not tell them
    /// apart; the component config rejects them for the same reason.
    InvalidNameKey,
};

struct RejectedVariable {
    Component component;
    Variable variable;
    RegistrationRejectionReason reason;
    /// \brief For AlreadyDefined: the owner that defines the variable, the owner of its component config entry or the
    /// runtime owner that registered it, the registering owner itself for a variable it defines twice
    std::string defined_by;
};

struct ComponentRegistrationResult {
    std::vector<RejectedVariable> rejected;
};

/// \brief Lets owners other than the component config add components to the device model at runtime. Registered
/// structure is held in memory only.
class DeviceModelRegistryInterface {
public:
    virtual ~DeviceModelRegistryInterface() = default;

    /// \brief Registers \p components under \p owner, replacing what \p owner registered before. Every variable has
    /// one definition: a variable that the component config or another runtime owner defines already is rejected.
    /// A variable with an empty instance, or of a component with an empty instance or an EVSE or connector id of 0, is
    /// rejected as well, and so is a second definition of a variable in \p components.
    /// The owner VARIABLE_OWNER_COMPONENT_CONFIG is reserved for libocpp and rejected.
    /// The variable listeners are notified of each Actual value that differs from the one \p owner registered before
    /// for the same variable.
    /// \param provider Receives the writes to the registered variables. Without a provider, writes are applied to the
    /// model like writes to any other variable. A write that is in progress while \p owner registers again is not
    /// stored, the registered value stays. The provider must stay valid until \p owner is unregistered or
    /// registers with another provider, which wait for the calls to the provider in progress on other threads to
    /// return.
    virtual ComponentRegistrationResult register_components(const std::string& owner,
                                                            const std::vector<RegisteredComponent>& components,
                                                            VariableValueProvider* provider) = 0;

    /// \brief Removes everything that \p owner registered. Returns once the calls to the provider of \p owner in
    /// progress on other threads returned; a provider may call it from its set_value. The reserved owner is ignored.
    virtual void unregister_components(const std::string& owner) = 0;

    /// \brief Reports a value that \p owner of a registered variable changed itself. Updates the model in
    /// memory and notifies the variable listeners; the provider is not called. The value must fit the data type and
    /// the allowed values of the variable, otherwise it is rejected. minLimit and maxLimit are not checked: they bound
    /// what the CSMS may set, not what the owner reports. The reserved owner is rejected.
    ///
    /// A change the owner makes while a write to the same attribute is in progress races with that write. To resolve
    /// it, the owner passes \p applied_write: the id of the last write to the attribute that it had applied when the
    /// change happened, or 0 if it had applied none since it registered. A report based on an older write than the
    /// last one DeviceModel stored is ignored, because that write replaced the reported value, and a write is not
    /// stored over a report based on it or on a newer write. Without \p applied_write the report is applied as it
    /// comes: the write that is in progress overwrites it if DeviceModel stores the write later, even if the owner
    /// changed the value after applying the write.
    /// \return Accepted also for a report that is ignored as outdated
    virtual SetVariableStatusEnum notify_value_changed(const Component& component, const Variable& variable,
                                                       AttributeEnum attribute, const std::string& value,
                                                       const std::string& owner,
                                                       std::optional<WriteId> applied_write) = 0;
};

} // namespace v2
} // namespace ocpp
