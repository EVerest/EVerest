// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>

#include <ocpp/v2/comparators.hpp>
#include <ocpp/v2/device_model_interface.hpp>
#include <ocpp/v2/device_model_storage_interface.hpp>

namespace ocpp {
namespace v2 {

/// \brief The monitors of the device model, kept in memory and in the storage. A monitor belongs to a component and
/// variable, not to the owner of the variable: it stays while its variable is not part of the model and applies again
/// when the variable returns. Not thread safe.
class MonitorStore {
public:
    using Monitors = std::unordered_map<std::int64_t, VariableMonitoringMeta>;

    explicit MonitorStore(DeviceModelStorageInterface& storage);

    /// \brief Loads every stored monitor
    void load();

    Monitors of(const Component& component, const Variable& variable) const;

    const StoredMonitor* find(std::int32_t monitor_id) const;

    /// \brief Stores a monitor, or replaces the one with the id of \p request. A delta monitor takes \p current_value
    /// as its reference.
    std::optional<VariableMonitoringMeta> set(const SetMonitoringData& request, VariableMonitorType type,
                                              const std::string& current_value);

    bool update_reference(std::int32_t monitor_id, const std::string& reference_value);

    ClearMonitoringStatusEnum clear(std::int32_t monitor_id, bool allow_protected);

    /// \brief Removes the custom monitors, also those of variables that are not part of the model (N03)
    std::int32_t clear_custom();

private:
    void insert(StoredMonitor monitor);
    void forget(std::int32_t monitor_id);

    DeviceModelStorageInterface& storage;
    std::map<std::int32_t, StoredMonitor> monitors;
    std::map<std::pair<Component, Variable>, std::set<std::int32_t>> by_variable;
};

} // namespace v2
} // namespace ocpp
