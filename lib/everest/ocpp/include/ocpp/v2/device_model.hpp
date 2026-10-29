// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#ifndef DEVICE_MODEL_HPP
#define DEVICE_MODEL_HPP

#include <map>
#include <mutex>
#include <optional>
#include <string>

#include <everest/logging.hpp>

#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model_abstract.hpp>
#include <ocpp/v2/device_model_composition.hpp>
#include <ocpp/v2/device_model_monitor_store.hpp>
#include <ocpp/v2/device_model_provider_calls.hpp>
#include <ocpp/v2/device_model_registry_interface.hpp>
#include <ocpp/v2/device_model_storage_interface.hpp>

namespace ocpp {
namespace v2 {

using on_variable_changed = std::function<void(
    const std::unordered_map<std::int64_t, VariableMonitoringMeta>& monitors, const Component& component,
    const Variable& variable, const VariableCharacteristics& characteristics, const VariableAttribute& attribute,
    const std::string& value_previous, const std::string& value_current)>;

using on_monitor_updated = std::function<void(const VariableMonitoringMeta& updated_monitor, const Component& component,
                                              const Variable& variable, const VariableCharacteristics& characteristics,
                                              const VariableAttribute& attribute, const std::string& current_value)>;

/// \brief This class manages access to the device model representation and to the device model interface and provides
/// functionality to support the use cases defined in the functional block Provisioning
class DeviceModel : public DeviceModelAbstract, public DeviceModelRegistryInterface {

private:
    // Serializes access to the view and the storage. Recursive: locked methods call back into
    // public methods (e.g. via DeviceModelInterface& composites). Invariant: never held while listeners are
    // invoked. The private methods require the mutex and never release it, except those that take a Lock, which
    // release it, and those documented to require it not to be held.
    mutable std::recursive_mutex mutex;

    std::unique_ptr<DeviceModelStorageInterface> storage;
    /// \brief The variables of the component config with their values. Their monitors are in \c monitors.
    DeviceModelMap component_config;
    /// \brief What each runtime owner registered
    std::map<std::string, OwnerRegistration> registrations;
    /// \brief The model the CSMS sees, composed from component_config and registrations
    ComposedModel view;
    MonitorStore monitors;
    ProviderCalls provider_calls;
    WriteId last_write_id{0};

    /// \brief Listener for the internal change of a variable
    std::vector<on_variable_changed> variable_listener;
    /// \brief Listener for the internal update of a monitor
    on_monitor_updated monitor_update_listener;

    /// \brief register_components without the check for the reserved owner
    ComponentRegistrationResult register_components_of(const std::string& owner,
                                                       const std::vector<RegisteredComponent>& components,
                                                       VariableValueProvider* provider);

    /// \brief Composes \c view again after component_config or registrations changed
    void recompose();

    const VariableMetaData* find_in_component_config(const Component& component, const Variable& variable) const;

    ComposedVariable* find_variable(const Component& component, const Variable& variable);
    const ComposedVariable* find_variable(const Component& component, const Variable& variable) const;

    /// \brief The variable of the view, or nullptr with \p unknown set to why it is not there
    template <typename Status>
    const ComposedVariable* find_variable(const Component& component, const Variable& variable, Status& unknown) const {
        const auto component_it = this->view.find(component);
        if (component_it == this->view.end()) {
            unknown = Status::UnknownComponent;
            return nullptr;
        }
        const auto variable_it = component_it->second.find(variable);
        if (variable_it == component_it->second.end()) {
            unknown = Status::UnknownVariable;
            return nullptr;
        }
        return &variable_it->second;
    }

    /// \brief The runtime owner that registered the variable, if any
    std::optional<std::string> runtime_owner(const Component& component, const Variable& variable) const;

    /// \brief Builds the variable that \p owner registers
    VariableMetaData build_registered_variable(const std::string& owner, const RegisteredVariable& registered_variable,
                                               std::vector<VariableAttribute> attributes);

    /// \brief The attribute of a variable with its current value, read from the storage for a variable that its
    /// backing keeps there
    std::optional<VariableAttribute> read_attribute(const Component& component, const Variable& variable,
                                                    const ComposedVariable& composed,
                                                    const AttributeEnum& attribute_enum) const;

    /// \brief All attributes of a variable with their current values, see read_attribute
    std::vector<VariableAttribute> read_attributes(const Component& component, const Variable& variable,
                                                   const ComposedVariable& composed) const;

    /// \brief A changed Actual value with everything the variable listeners receive, copied under the mutex
    struct ValueChange {
        std::vector<on_variable_changed> listeners;
        MonitorStore::Monitors monitors;
        VariableCharacteristics characteristics;
        VariableAttribute attribute;
        std::string value_previous;
    };

    /// \brief Invokes the variable listeners for \p change. Requires the mutex not to be held.
    static void notify_variable_listeners(const ValueChange& change, const Component& component,
                                          const Variable& variable, const std::string& value);

    /// \brief The result of apply
    struct AppliedWrite {
        SetVariableStatusEnum status{SetVariableStatusEnum::Rejected};
        /// \brief What the variable listeners receive, when an Actual value changed
        std::optional<ValueChange> change;
    };

    using Lock = std::unique_lock<std::recursive_mutex>;

    /// \brief What a write may do
    struct WriteRules {
        /// \brief Whether a ReadOnly attribute may be written
        bool allow_read_only{false};
        /// \brief Whether a variable that a runtime owner registered may be written: true for the CSMS and for an
        /// owner writing its own variable, false for the local channel
        bool allow_registered{false};
    };

    static constexpr WriteRules CSMS_WRITE{/*allow_read_only=*/false, /*allow_registered=*/true};
    /// \brief The local channel writes the component config only: a registered variable is changed by its owner,
    /// through notify_value_changed or its provider, or by the CSMS
    static constexpr WriteRules LOCAL_WRITE{/*allow_read_only=*/true, /*allow_registered=*/false};

    /// \brief Checks a write of \p value against the characteristics and the mutability of the variable and \p rules
    /// \return The variable to write to, or nullptr with the reason in \p status
    const ComposedVariable* check_write(const Component& component, const Variable& variable,
                                        const AttributeEnum& attribute_enum, const std::string& value,
                                        const WriteRules& rules, SetVariableStatusEnum& status);

    /// \brief check_write, then write or forward_to_provider by the backing of the variable
    SetVariableStatusEnum check_and_write(Lock lock, const Component& component, const Variable& variable,
                                          const AttributeEnum& attribute_enum, const std::string& value,
                                          const std::string& source, const WriteRules& rules);

    /// \brief Writes \p value to the model: apply, then releases \p lock and notifies the variable listeners
    SetVariableStatusEnum write(Lock lock, const ComposedVariable& composed, const Component& component,
                                const Variable& variable, const AttributeEnum& attribute_enum, const std::string& value,
                                const std::string& source);

    /// \brief Forwards the write of \p value to the provider of the variable after releasing \p lock, then stores the
    /// value if the provider accepted it, see store_provided_value
    /// \return The result of the provider
    SetVariableStatusEnum forward_to_provider(Lock lock, const ComposedVariable& composed, const Component& component,
                                              const Variable& variable, const AttributeEnum& attribute_enum,
                                              const std::string& value, const std::string& source);

    /// \brief Stores \p value, which the provider of \p owner accepted with \p write_id, unless \p owner registered
    /// again or reported a value based on this or a newer write since. Requires the mutex not to be held.
    void store_provided_value(const std::string& owner, WriteId write_id, const Component& component,
                              const Variable& variable, const AttributeEnum& attribute_enum, const std::string& value,
                              const std::string& source);

    /// \brief Updates the model with \p value: persist, then, only if that is Accepted, the value in memory
    AppliedWrite apply(const ComposedVariable& composed, const Component& component, const Variable& variable,
                       const AttributeEnum& attribute_enum, const std::string& value, const std::string& source);

    /// \brief Writes \p value to where the backing of the variable keeps it beyond memory: the storage of the
    /// component config or of its owner. Writes nothing and returns Accepted for a value held in memory only: a
    /// non-persistent attribute, or a registered variable.
    SetVariableStatusEnum persist(const ComposedVariable& composed, const Component& component,
                                  const Variable& variable, const AttributeEnum& attribute_enum,
                                  const std::string& value, const std::string& source,
                                  const VariableAttribute& attribute);

    /// \brief Iterates over the given \p component_criteria and converts this to the variable names
    /// (Active,Available,Enabled,Problem). If any of the variables can not be found as part of a component this
    /// function returns false. If any of those variable's value is true, this function returns true (except for
    /// criteria problem). If all variable's value are false, this function returns false
    ///  \param component_id
    ///  \param /// component_criteria
    ///  \return
    bool component_criteria_match(const Component& component_id,
                                  const std::vector<ComponentCriterionEnum>& component_criteria);

    ///
    /// \brief Helper function to check if a variable has a value.
    /// \param component_variable   Component variable to check.
    /// \param attribute            Attribute to check.
    ///
    /// \throws DeviceModelError if variable has no value or value is an empty string.
    ///
    void check_variable_has_value(const ComponentVariable& component_variable,
                                  const AttributeEnum attribute = AttributeEnum::Actual);

    ///
    /// \brief Helper function to check if a required variable has a value.
    /// \param required_variable    Required component variable to check.
    /// \param supported_versions   The current supported ocpp versions.
    /// \throws DeviceModelError if variable has no value or value is an empty string.
    ///
    void check_required_variable(const RequiredComponentVariable& required_variable,
                                 const std::vector<OcppProtocolVersion>& supported_versions);

    ///
    /// \brief Loop over all required variables to check if they have a value.
    ///
    /// This will check for all required variables from `ctrlr_component_variables.cpp` `required_variables`.
    /// It will also check for specific required variables that belong to a specific controller. If a controller is not
    /// available, the 'required' variables of that component are not required at this point.
    ///
    /// \throws DeviceModelError if one of the variables does not have a value or value is an empty string.
    ///
    void check_required_variables();

    /// \brief Resolve the identity for \p slot.
    /// Per-slot Identity overrides SecurityCtrlr.Identity (B09.FR.16-18).
    std::string resolve_identity(std::int32_t slot);

    /// \brief Resolve the basic-auth password for \p slot.
    /// Per-slot BasicAuthPassword overrides the global BasicAuthPassword (B09.FR.26-28).
    std::optional<std::string> resolve_basic_auth_password(std::int32_t slot);

public:
    /// \brief Constructor for the device model
    /// \param device_model_storage_interface pointer to a device model interface class
    explicit DeviceModel(std::unique_ptr<DeviceModelStorageInterface> device_model_storage_interface);
    ~DeviceModel() override;
    DeviceModel(const DeviceModel&) = delete;
    DeviceModel& operator=(const DeviceModel&) = delete;
    DeviceModel(DeviceModel&&) = delete;
    DeviceModel& operator=(DeviceModel&&) = delete;

    GetVariableStatusEnum get_variable(const Component& component_id, const Variable& variable_id,
                                       const AttributeEnum& attribute_enum, std::string& value, bool allow_write_only,
                                       UnsetValue unset_value) const override;

    SetVariableStatusEnum set_value(const Component& component_id, const Variable& variable_id,
                                    const AttributeEnum& attribute_enum, const std::string& value,
                                    const std::string& source, bool allow_read_only = false) override;

    bool create_network_configuration_slot_from_default_schema(std::int32_t new_slot) override;

    SetVariableStatusEnum set_read_only_value(const Component& component_id, const Variable& variable_id,
                                              const AttributeEnum& attribute_enum, const std::string& value,
                                              const std::string& source) override;

    SetVariableStatusEnum clear_value(const Component& component_id, const Variable& variable_id,
                                      const AttributeEnum& attribute_enum, const std::string& source) override;

    std::optional<MutabilityEnum> get_mutability(const Component& component_id, const Variable& variable_id,
                                                 const AttributeEnum& attribute_enum) override;

    std::optional<VariableMetaData> get_variable_meta_data(const Component& component_id,
                                                           const Variable& variable_id) override;

    std::vector<ReportData> get_base_report_data(const ReportBaseEnum& report_base) override;

    std::vector<ReportData> get_custom_report_data(
        const std::optional<std::vector<ComponentVariable>>& component_variables = std::nullopt,
        const std::optional<std::vector<ComponentCriterionEnum>>& component_criteria = std::nullopt) override;

    std::vector<SetMonitoringResult>
    set_monitors(const std::vector<SetMonitoringData>& requests,
                 const VariableMonitorType type = VariableMonitorType::CustomMonitor) override;

    bool update_monitor_reference(std::int32_t monitor_id, const std::string& reference_value) override;

    std::vector<VariableMonitoringPeriodic> get_periodic_monitors() override;

    std::vector<MonitoringData> get_monitors(const std::vector<MonitoringCriterionEnum>& criteria,
                                             const std::vector<ComponentVariable>& component_variables) override;

    std::vector<ClearMonitoringResult> clear_monitors(const std::vector<int>& request_ids,
                                                      bool allow_protected = false) override;

    std::int32_t clear_custom_monitors() override;

    void register_variable_listener(on_variable_changed&& listener) override {
        std::lock_guard<std::recursive_mutex> lock(this->mutex);
        variable_listener.push_back(std::move(listener));
    }

    void register_monitor_listener(on_monitor_updated&& listener) override {
        std::lock_guard<std::recursive_mutex> lock(this->mutex);
        monitor_update_listener = std::move(listener);
    }

    void check_integrity(const std::map<std::int32_t, std::int32_t>& evse_connector_structure) override;

    // DeviceModelRegistryInterface
    ComponentRegistrationResult register_components(const std::string& owner,
                                                    const std::vector<RegisteredComponent>& components,
                                                    VariableValueProvider* provider) override;
    void unregister_components(const std::string& owner) override;
    SetVariableStatusEnum notify_value_changed(const Component& component, const Variable& variable,
                                               AttributeEnum attribute, const std::string& value,
                                               const std::string& owner, std::optional<WriteId> applied_write) override;

    // ConnectivityManagerConfiguration interface
    std::string get_network_configuration_priority() override;
    void set_network_configuration_priority(const std::string& priority, const std::string& source) override;
    std::optional<ocpp::v2::NetworkConnectionProfile> read_network_connection_profile(std::int32_t slot) override;
    bool write_network_connection_profile(std::int32_t slot, const ocpp::v2::NetworkConnectionProfile& profile,
                                          const std::string& source) override;
    void clear_network_connection_profile(std::int32_t slot) override;
    bool get_allow_security_level_zero_connections() override;
    std::int32_t get_security_profile() override;
    std::optional<std::int32_t> get_network_config_timeout() override;
    std::optional<WebsocketConnectionOptions> get_websocket_connection_options(std::int32_t slot) override;
    void set_active_security_profile(std::int32_t security_profile, const std::string& source) override;
    void set_active_network_profile_slot(std::int32_t slot, const std::string& source) override;
    std::optional<std::int32_t> get_active_network_profile_slot() override;
    void set_per_slot_ocpp_version(std::int32_t slot, const std::string& version, const std::string& source) override;
    void set_security_ctrl_security_profile(std::int32_t security_profile, const std::string& source) override;
    void set_security_ctrl_identity(const std::string& identity, const std::string& source) override;
    void set_active_message_timeout(std::int32_t message_timeout_s, const std::string& source) override;
};

} // namespace v2
} // namespace ocpp

#endif // DEVICE_MODEL_HPP
