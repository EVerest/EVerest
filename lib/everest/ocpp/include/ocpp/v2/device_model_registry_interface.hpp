// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <ocpp/v2/device_model_interface.hpp>
#include <ocpp/v2/device_model_storage_interface.hpp>
#include <ocpp/v2/ocpp_types.hpp>

namespace ocpp {
namespace v2 {

/// \brief Source tag of the variables that libocpp registers itself
inline constexpr const char* VARIABLE_SOURCE_OCPP_RUNTIME = "OCPP_RUNTIME";

/// \brief Owner of registered variables whose values are stored outside of the device model. DeviceModel forwards
/// every validated write of such a variable to its provider.
class VariableValueProvider {
public:
    virtual ~VariableValueProvider() = default;

    /// \brief Applies \p value to \p attribute of \p variable on \p component at the owner. Called without the
    /// DeviceModel lock; writes to one variable are serialised. The model in memory is updated only when the result is
    /// Accepted or RebootRequired.
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
    /// without \c type is the Actual attribute. A persistent attribute of a source with a provider is kept across
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
    /// \brief Another runtime source registered the variable already
    OwnedByOtherSource,
    /// \brief The variable has no attributes
    NoAttributes,
};

struct RejectedVariable {
    Component component;
    Variable variable;
    RegistrationRejectionReason reason;
};

struct ComponentRegistrationResult {
    std::vector<RejectedVariable> rejected;
};

/// \brief Lets owners other than OCPP add components to the device model at runtime. Registered structure is held in
/// memory only.
class DeviceModelRegistryInterface {
public:
    virtual ~DeviceModelRegistryInterface() = default;

    /// \brief Registers \p components under \p source, replacing what \p source registered before. A registered
    /// variable overrides a variable of the same name from the component config. Variables that another runtime source
    /// registered already are rejected.
    /// \param provider Receives the writes to the registered variables. Without a provider, writes are applied to the
    /// model like writes to any other variable. The provider must stay valid until \p source is unregistered or
    /// registers with another provider.
    virtual ComponentRegistrationResult register_components(const std::string& source,
                                                            const std::vector<RegisteredComponent>& components,
                                                            VariableValueProvider* provider) = 0;

    /// \brief Removes everything that \p source registered. Variables of the component config that a registration
    /// overrode become visible again. Returns once the calls to the provider of \p source in progress returned. A
    /// provider must therefore not call it from its set_value, nor register_components with another provider, nor
    /// write the variable it is being asked to set: all three wait for its own call to return.
    virtual void unregister_components(const std::string& source) = 0;

    /// \brief Reports a value that the owner \p source of a registered variable changed itself. Updates the model in
    /// memory and notifies the variable listeners; the provider is not called. The value must fit the data type and
    /// the allowed values of the variable, otherwise it is rejected. minLimit and maxLimit are not checked: they bound
    /// what the CSMS may set, not what the owner reports.
    virtual SetVariableStatusEnum notify_value_changed(const Component& component, const Variable& variable,
                                                       AttributeEnum attribute, const std::string& value,
                                                       const std::string& source) = 0;
};

} // namespace v2
} // namespace ocpp
