// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <vector>

#include <everest/logging.hpp>
#include <ocpp/v2/device_model_monitor_store.hpp>

namespace ocpp {
namespace v2 {

MonitorStore::MonitorStore(DeviceModelStorageInterface& storage) : storage{storage} {
}

void MonitorStore::load() {
    this->monitors.clear();
    this->by_variable.clear();
    for (auto& stored : this->storage.get_all_monitors()) {
        this->insert(std::move(stored));
    }
}

MonitorStore::Monitors MonitorStore::of(const Component& component, const Variable& variable) const {
    Monitors result;
    const auto ids_it = this->by_variable.find({component, variable});
    if (ids_it == this->by_variable.end()) {
        return result;
    }
    for (const auto id : ids_it->second) {
        result.emplace(id, this->monitors.at(id).monitor);
    }
    return result;
}

const StoredMonitor* MonitorStore::find(const std::int32_t monitor_id) const {
    const auto it = this->monitors.find(monitor_id);
    return it == this->monitors.end() ? nullptr : &it->second;
}

std::optional<VariableMonitoringMeta>
MonitorStore::set(const SetMonitoringData& request, const VariableMonitorType type, const std::string& current_value) {
    auto monitor_meta = this->storage.set_monitoring_data(request, type);
    if (!monitor_meta.has_value()) {
        return std::nullopt;
    }
    const auto id = monitor_meta->monitor.id;

    // The storage takes the reference of a delta monitor from its own copy of the value
    if (request.type == MonitorEnum::Delta and monitor_meta->reference_value != current_value) {
        if (this->storage.update_monitoring_reference(id, current_value)) {
            monitor_meta->reference_value = current_value;
        } else {
            EVLOG_warning << "Could not update the reference value of delta monitor: " << id;
        }
    }

    this->forget(id);
    this->insert(StoredMonitor{request.component, request.variable, monitor_meta.value()});
    return monitor_meta;
}

bool MonitorStore::update_reference(const std::int32_t monitor_id, const std::string& reference_value) {
    const auto it = this->monitors.find(monitor_id);
    if (it == this->monitors.end() or !this->storage.update_monitoring_reference(monitor_id, reference_value)) {
        return false;
    }
    it->second.monitor.reference_value = reference_value;
    return true;
}

ClearMonitoringStatusEnum MonitorStore::clear(const std::int32_t monitor_id, const bool allow_protected) {
    const auto status = this->storage.clear_variable_monitor(monitor_id, allow_protected);
    if (status == ClearMonitoringStatusEnum::Accepted) {
        this->forget(monitor_id);
    }
    return status;
}

std::int32_t MonitorStore::clear_custom() {
    const auto deleted = this->storage.clear_custom_variable_monitors();
    std::vector<std::int32_t> custom;
    for (const auto& [id, stored] : this->monitors) {
        if (stored.monitor.type == VariableMonitorType::CustomMonitor) {
            custom.push_back(id);
        }
    }
    for (const auto id : custom) {
        this->forget(id);
    }
    return deleted;
}

void MonitorStore::insert(StoredMonitor monitor) {
    const auto id = monitor.monitor.monitor.id;
    this->by_variable[{monitor.component, monitor.variable}].insert(id);
    this->monitors.insert_or_assign(id, std::move(monitor));
}

void MonitorStore::forget(const std::int32_t monitor_id) {
    const auto it = this->monitors.find(monitor_id);
    if (it == this->monitors.end()) {
        return;
    }
    const auto ids_it = this->by_variable.find({it->second.component, it->second.variable});
    if (ids_it != this->by_variable.end()) {
        ids_it->second.erase(monitor_id);
        if (ids_it->second.empty()) {
            this->by_variable.erase(ids_it);
        }
    }
    this->monitors.erase(it);
}

} // namespace v2
} // namespace ocpp
