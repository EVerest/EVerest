// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <ocpp/v2/device_model_interface.hpp>
#include <ocpp/v2/device_model_storage_interface.hpp>
#include <ocpp/v2/ocpp_types.hpp>

namespace ocpp {
namespace v2 {

/// \brief Receives the writes to the variables of a runtime owner
class VariableValueProvider {
public:
    virtual ~VariableValueProvider() = default;

    /// \brief Applies \p value to \p attribute of \p variable on \p component at the owner. Called without the
    /// DeviceModel lock on the thread of the caller, a SetVariables.req for example, which waits for it: DeviceModel
    /// sets no deadline, so a provider answers in time itself, well within the message timeout of the CSMS, and
    /// rejects a write it cannot complete by then. Calls are not serialised, also not for one variable.
    ///
    /// Accepted means that the owner applied exactly \p value: DeviceModel then stores it in memory, so the owner does
    /// not report it. An owner that would apply another value, a clamped one for example, rejects the write.
    /// RebootRequired means that the owner holds \p value for the next start and registers with it then. DeviceModel
    /// keeps the value in effect until then, and so must the owner: it does not report the held value with
    /// notify_value_changed before the next start.
    ///
    /// DeviceModel stores an accepted value once this call returned. When writes to one variable overlap, the value
    /// stored last stays in memory.
    /// \param source The source of the value (e.g. 'csms', 'internal')
    virtual SetVariableStatusEnum set_value(const Component& component, const Variable& variable,
                                            AttributeEnum attribute, const std::string& value,
                                            const std::string& source) = 0;
};

/// \brief A variable that an owner registers at runtime
struct RegisteredVariable {
    Variable variable;
    VariableCharacteristics characteristics;
    /// \brief The attributes with their initial values. An attribute without \c persistent is not persistent, one
    /// without \c type is the Actual attribute. A persistent attribute of an owner with a provider is kept across
    /// restarts by the owner: it is reported as persistent, but DeviceModel does not store its value.
    std::vector<VariableAttribute> attributes;
    /// \brief Hardwired and preconfigured monitors of the variable. A stored monitor of the same type and severity
    /// (N04.FR.10), a custom monitor the CSMS set included, is kept with its id and the changes the CSMS made to it;
    /// the id and the reference value given here are ignored.
    std::vector<VariableMonitoringMeta> monitors;
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
    /// \brief libocpp writes the variable from its own state, AuthCacheCtrlr.Storage, the AvailabilityState of an EVSE
    /// or any variable of a NetworkConfiguration slot for example, so only the component config defines it
    ReservedVariable,
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

    /// \brief Registers \p components under \p owner and replaces all previous entries for this owner. A variable
    /// that is no longer registered, because it is left out or rejected, loses its monitors and stored values;
    /// unregister_components keeps them. The variable listeners are notified of each Actual value that changed.
    /// Every variable has one definition: a variable that the component config or another runtime owner defines
    /// already is rejected, and so is a second definition in \p components. Also rejected are a variable with an empty
    /// instance or of a component with an empty instance or an EVSE or connector id of 0, the reserved owner
    /// VARIABLE_OWNER_COMPONENT_CONFIG, and the variables that libocpp writes from its own state, see
    /// RegistrationRejectionReason::ReservedVariable.
    /// \param provider Receives the writes to the registered variables. Without a provider, writes are applied to the
    /// model like writes to any other variable. A write that is in progress while \p owner registers again is not
    /// stored, the registered value stays. DeviceModel shares the ownership of the provider until \p owner is
    /// unregistered or registers with another provider, and until the calls to it in progress return; unregistering
    /// and registering with another provider wait for the calls in progress on other threads.
    virtual ComponentRegistrationResult register_components(const std::string& owner,
                                                            const std::vector<RegisteredComponent>& components,
                                                            std::shared_ptr<VariableValueProvider> provider) = 0;

    /// \brief Removes everything that \p owner registered. The monitors and stored values of its variables are kept
    /// for its next registration. Returns once the calls to the provider of \p owner in
    /// progress on other threads returned; a provider may call it from its set_value. The reserved owner is ignored.
    virtual void unregister_components(const std::string& owner) = 0;

    /// \brief Reports a value that \p owner of a registered variable changed itself. Updates the model in
    /// memory and notifies the variable listeners; the provider is not called. The value must fit the data type and
    /// the allowed values of the variable, otherwise it is rejected. minLimit and maxLimit are not checked: they bound
    /// what the CSMS may set, not what the owner reports. The reserved owner is rejected.
    ///
    /// A report made while a write to the same attribute is in progress is overwritten when DeviceModel stores that
    /// write, after set_value returned. An owner whose change must outlast the write reports it after set_value
    /// returned.
    virtual SetVariableStatusEnum notify_value_changed(const Component& component, const Variable& variable,
                                                       AttributeEnum attribute, const std::string& value,
                                                       const std::string& owner) = 0;
};

} // namespace v2
} // namespace ocpp
