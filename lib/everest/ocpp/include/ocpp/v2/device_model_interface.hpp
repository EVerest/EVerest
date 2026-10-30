// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#ifndef DEVICE_MODEL_INTERFACE_HPP
#define DEVICE_MODEL_INTERFACE_HPP

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <ocpp/v2/enums.hpp>
#include <ocpp/v2/ocpp_types.hpp>

namespace ocpp {
namespace v2 {

/// \brief Helper struct that holds values that don't have spec coverage
struct VariableMonitoringMeta {
    VariableMonitoring monitor;
    VariableMonitorType type;
    std::optional<std::string> reference_value;
};

/// \brief Helper struct that contains all monitors related to a variable that are of a periodic type
struct VariableMonitoringPeriodic {
    Component component;
    Variable variable;
    std::vector<VariableMonitoringMeta> monitors;
};

/// \brief The owner of the variables the component config defines, OCPPCommCtrlr.HeartbeatInterval or
/// SecurityCtrlr.Identity for example. DeviceModel holds their values in memory and persists them itself. A variable of
/// the component config that the storage reports with another owner, the EVerest device model of the OCPP modules for
/// example, is held by the storage of that owner, which changes its value without DeviceModel.
inline constexpr auto VARIABLE_OWNER_COMPONENT_CONFIG = "COMPONENT_CONFIG";

/// \brief Helper struct that combines VariableCharacteristics, VariableMonitoring and the VariableAttribute(s)
struct VariableMetaData {
    VariableCharacteristics characteristics;
    std::unordered_map<std::int64_t, VariableMonitoringMeta> monitors;
    /// \brief The owner of the variable: VARIABLE_OWNER_COMPONENT_CONFIG unless the storage or a registration names
    /// another
    std::string owner{VARIABLE_OWNER_COMPONENT_CONFIG};
    /// \brief The attributes of the variable with their current values
    std::vector<VariableAttribute> attributes;
};

using VariableMap = std::map<Variable, VariableMetaData>;
using DeviceModelMap = std::map<Component, VariableMap>;

class DeviceModelError : public std::exception {
public:
    [[nodiscard]] const char* what() const noexcept override {
        return this->reason.c_str();
    }
    explicit DeviceModelError(std::string msg) : reason(std::move(msg)) {
    }
    explicit DeviceModelError(const char* msg) : reason(std::string(msg)) {
    }

private:
    std::string reason;
};

/// \brief How a read answers for an attribute that exists but has no value
enum class UnsetValue {
    NotSupported, ///< NotSupportedAttributeType, the value is absent
    EmptyString,  ///< Accepted with an empty string, as GetVariables reports it (B06.FR.13)
};

/// \brief Access to the variables of the device model: values, metadata, reports and variable listeners
class DeviceModelVariablesInterface {
public:
    virtual ~DeviceModelVariablesInterface() = default;

    // ============================================================================
    // Value getters
    // ============================================================================

    /// \brief Gets a variable attribute value
    /// \param component_id The component
    /// \param variable_id The variable
    /// \param attribute_enum The attribute
    /// \param value The value to get (as string)
    /// \param allow_write_only If true, write-only variables can be accessed; otherwise a write-only variable
    /// answers Rejected (B06.FR.09), whether or not it has a value
    /// \param unset_value How an attribute that exists but has no value is answered
    /// \return Result of the requested operation
    virtual GetVariableStatusEnum get_variable(const Component& component_id, const Variable& variable_id,
                                               const AttributeEnum& attribute_enum, std::string& value,
                                               bool allow_write_only, UnsetValue unset_value) const = 0;

    // ============================================================================
    // Value setters
    // ============================================================================

    /// \brief Sets a variable attribute value
    /// \param component_id The component
    /// \param variable_id The variable
    /// \param attribute_enum The attribute
    /// \param value The value to set (as string)
    /// \param source The source of the value (e.g., 'csms', 'default', 'internal')
    /// \param allow_read_only_update If true, ReadOnly variables can be changed. A ReadOnly mutability binds
    ///        SetVariables from the CSMS only: libocpp passes true for its own writes and for CSMS messages whose
    ///        purpose is to set a ReadOnly variable, such as SetMonitoringBase and SetMonitoringLevel; a write to a
    ///        variable that a runtime owner registered reaches that owner.
    /// \return Accepted when the value is applied, RebootRequired when it is set but takes effect at the next start.
    ///         Both store the value in memory and notify the variable listeners, whoever owns the variable, so a read
    ///         returns the value written. Any other result notifies no listener and leaves the value DeviceModel holds
    ///         unchanged. A variable that the storage of another owner holds is always read from that storage, which
    ///         decides what it reports until the next start.
    virtual SetVariableStatusEnum set_value(const Component& component_id, const Variable& variable_id,
                                            const AttributeEnum& attribute_enum, const std::string& value,
                                            const std::string& source, bool allow_read_only_update = false) = 0;

    /// \brief set_value with allow_read_only_update for the local channel, ChargePoint::set_variables, which writes the
    /// component config only: a variable that a runtime owner registered is Rejected
    virtual SetVariableStatusEnum set_local_value(const Component& component_id, const Variable& variable_id,
                                                  const AttributeEnum& attribute_enum, const std::string& value,
                                                  const std::string& source) {
        return this->set_value(component_id, variable_id, attribute_enum, value, source, true);
    }

    /// \brief Sets a read-only variable attribute value (only works on certain allowed components)
    /// \param component_id The component
    /// \param variable_id The variable
    /// \param attribute_enum The attribute
    /// \param value The value to set (as string)
    /// \param source The source of the value (e.g., 'csms', 'default', 'internal')
    /// \return Result of the requested operation
    virtual SetVariableStatusEnum set_read_only_value(const Component& component_id, const Variable& variable_id,
                                                      const AttributeEnum& attribute_enum, const std::string& value,
                                                      const std::string& source) = 0;

    /// \brief Clear the Actual attribute of \p variable on \p component, bypassing
    /// validate_value AND the read-only mutability check. Intended for spec-mandated
    /// clears where the empty-string sentinel would otherwise fail length / range
    /// constraints (e.g. B09.FR.27 clearing a per-slot BasicAuthPassword that has
    /// minLimit=16). Do not use to clear ReadOnly attributes outside of an explicit
    /// spec-mandated clear path: this method does not enforce the mutability
    /// allowlist that set_read_only_value uses.
    /// \param source identifier for the change source ("internal" / OCPP source)
    /// \return SetVariableStatusEnum reflecting the storage write outcome
    virtual SetVariableStatusEnum clear_value(const Component& component_id, const Variable& variable_id,
                                              const AttributeEnum& attribute_enum, const std::string& source) = 0;

    // ============================================================================
    // Metadata and reports
    // ============================================================================

    /// \brief Get the mutability for the given component, variable and attribute
    /// \param component_id The component
    /// \param variable_id The variable
    /// \param attribute_enum The attribute
    /// \return The mutability of the given component variable, or std::nullopt
    virtual std::optional<MutabilityEnum> get_mutability(const Component& component_id, const Variable& variable_id,
                                                         const AttributeEnum& attribute_enum) = 0;

    /// \brief Gets the VariableMetaData for the given component and variable
    /// \param component_id The component
    /// \param variable_id The variable
    /// \return VariableMetaData or std::nullopt if not present
    virtual std::optional<VariableMetaData> get_variable_meta_data(const Component& component_id,
                                                                   const Variable& variable_id) = 0;

    /// \brief Gets the ReportData for the specified report base
    /// \param report_base The report base filter
    /// \return Vector of ReportData
    virtual std::vector<ReportData> get_base_report_data(const ReportBaseEnum& report_base) = 0;

    /// \brief Gets the ReportData for the specified filters
    /// \param component_variables Optional component variables filter
    /// \param component_criteria Optional component criteria filter
    /// \return Vector of ReportData
    virtual std::vector<ReportData> get_custom_report_data(
        const std::optional<std::vector<ComponentVariable>>& component_variables = std::nullopt,
        const std::optional<std::vector<ComponentCriterionEnum>>& component_criteria = std::nullopt) = 0;

    /// \brief Registers a listener for variable changes
    /// \param listener Called after the Actual value of a variable changed, with the attribute as it is after the
    ///                 write: its value equals value_current, not value_previous
    virtual void register_variable_listener(
        std::function<void(const std::unordered_map<std::int64_t, VariableMonitoringMeta>& monitors,
                           const Component& component, const Variable& variable,
                           const VariableCharacteristics& characteristics, const VariableAttribute& attribute,
                           const std::string& value_previous, const std::string& value_current)>&& listener) = 0;

    /// \brief Create a fresh NetworkConfiguration_<new_slot> from the embedded default schema.
    /// \param new_slot  New NetworkConfiguration instance to create.
    /// \return true on success; false on failure.
    virtual bool create_network_configuration_slot_from_default_schema(std::int32_t /*new_slot*/) {
        return false;
    }
};

/// \brief Access to the monitors of the device model. Monitors are evaluated when a value changes, so monitoring
/// builds on variable access.
class DeviceModelMonitoringInterface : public DeviceModelVariablesInterface {
public:
    // ============================================================================
    // Monitoring operations
    // ============================================================================

    /// \brief Sets the given monitor requests in the device model
    /// \param requests The monitoring requests
    /// \param type The type of monitors (HardWiredMonitor, PreconfiguredMonitor, CustomMonitor)
    /// \return List of results of the requested operation
    virtual std::vector<SetMonitoringResult>
    set_monitors(const std::vector<SetMonitoringData>& requests,
                 const VariableMonitorType type = VariableMonitorType::CustomMonitor) = 0;

    /// \brief Updates the reference value for a monitor
    /// \param monitor_id The monitor ID
    /// \param reference_value The new reference value
    /// \return true if successful, false otherwise
    virtual bool update_monitor_reference(std::int32_t monitor_id, const std::string& reference_value) = 0;

    /// \brief Gets all periodic monitors
    /// \return Vector of periodic monitors
    virtual std::vector<VariableMonitoringPeriodic> get_periodic_monitors() = 0;

    /// \brief Gets the monitoring data for the requested criteria and component variables
    /// \param criteria The monitoring criteria
    /// \param component_variables The component variables to get monitors for
    /// \return List of monitoring data
    virtual std::vector<MonitoringData> get_monitors(const std::vector<MonitoringCriterionEnum>& criteria,
                                                     const std::vector<ComponentVariable>& component_variables) = 0;

    /// \brief Clears the given monitor IDs from the registered monitors
    /// \param request_ids The monitor IDs to clear
    /// \param allow_protected If true, non-custom monitors can be deleted
    /// \return List of results of the requested operation
    virtual std::vector<ClearMonitoringResult> clear_monitors(const std::vector<int>& request_ids,
                                                              bool allow_protected = false) = 0;

    /// \brief Clears all custom monitors (set by the CSMS)
    /// \return Count of monitors deleted
    virtual std::int32_t clear_custom_monitors() = 0;

    /// \brief Registers a listener for monitor updates
    /// \param listener The listener function to register
    virtual void register_monitor_listener(
        std::function<void(const VariableMonitoringMeta& updated_monitor, const Component& component,
                           const Variable& variable, const VariableCharacteristics& characteristics,
                           const VariableAttribute& attribute, const std::string& current_value)>&& listener) = 0;
};

/// \brief Pure abstract interface for device model access.
class DeviceModelInterface : public DeviceModelMonitoringInterface {
public:
    // ============================================================================
    // Integrity check
    // ============================================================================

    /// \brief Check data integrity of the device model
    /// \param evse_connector_structure The EVSE connector structure
    virtual void check_integrity(const std::map<std::int32_t, std::int32_t>& evse_connector_structure) = 0;

    /// \brief Removes the monitors of variables that are not part of the device model and the stored values of
    /// variables that no runtime owner registers. Every runtime owner must have registered before.
    /// \note N04, remarks: a monitor is persistent after a firmware update if the monitored variable still exists
    /// and is still monitor-able, otherwise it is removed. Whether a monitor still fits its variable is checked when
    /// the owner registers the variable.
    virtual void remove_monitors_and_values_of_absent_variables() = 0;
};

} // namespace v2
} // namespace ocpp

#endif // DEVICE_MODEL_INTERFACE_HPP
