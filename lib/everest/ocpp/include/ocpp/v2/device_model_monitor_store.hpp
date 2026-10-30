// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>

#include <ocpp/v2/comparators.hpp>
#include <ocpp/v2/device_model_interface.hpp>
#include <ocpp/v2/device_model_registry_interface.hpp>
#include <ocpp/v2/device_model_storage_interface.hpp>

namespace ocpp {
namespace v2 {

/// \brief The monitors of the device model, kept in memory and in the storage. A monitor is keyed by component and
/// variable, not by the owner of the variable: it applies to the variable whoever registers it. A monitor whose
/// variable is not part of the model is kept until remove_if removes it. Not thread safe.
class MonitorStore {
public:
    using Monitors = std::unordered_map<std::int64_t, VariableMonitoringMeta>;
    using MonitorValidator = std::function<bool(MonitorEnum type, float value)>;

    explicit MonitorStore(DeviceModelStorageInterface& storage);

    /// \brief Loads every stored monitor
    void load();

    Monitors of(const Component& component, const Variable& variable) const;

    const StoredMonitor* find(std::int32_t monitor_id) const;

    /// \brief Stores a monitor, or replaces the one with the id of \p request. A delta monitor takes
    /// \p current_value as its reference.
    std::optional<VariableMonitoringMeta> set(const SetMonitoringData& request, VariableMonitorType type,
                                              const std::string& current_value);

    bool update_reference(std::int32_t monitor_id, const std::string& reference_value);

    ClearMonitoringStatusEnum clear(std::int32_t monitor_id, bool allow_protected);

    /// \brief Removes the custom monitors, also those of variables that are not part of the model (N03)
    std::int32_t clear_custom();

    /// \brief Brings the monitors of a variable that \p owner registers in line with the registration: stored
    /// monitors that \p fits rejects are removed (N04), and so are hardwired and preconfigured monitors that the
    /// registration no longer lists. The monitors the registration lists are added if \p is_valid accepts them.
    void reconcile(const std::string& owner, const Component& component, const RegisteredVariable& registered_variable,
                   const std::optional<std::string>& actual_value, const MonitorValidator& fits,
                   const MonitorValidator& is_valid);

    /// \brief Removes the monitors for which \p remove returns true, logging \p reason with each
    void remove_if(const std::function<bool(const StoredMonitor&)>& remove, const std::string& reason);

private:
    void insert(StoredMonitor monitor);
    /// \brief Removes the monitor from the storage, and from memory only if that succeeded
    void remove(std::int32_t monitor_id);
    void forget(std::int32_t monitor_id);

    DeviceModelStorageInterface& storage;
    std::map<std::int32_t, StoredMonitor> monitors;
    std::map<std::pair<Component, Variable>, std::set<std::int32_t>> by_variable;
};

} // namespace v2
} // namespace ocpp
