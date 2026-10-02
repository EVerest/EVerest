// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <algorithm>
#include <vector>

#include <everest/database/exceptions.hpp>
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

std::optional<VariableMonitoringMeta> MonitorStore::set(const SetMonitoringData& request,
                                                        const VariableMonitorType type,
                                                        const std::optional<std::string>& source,
                                                        const std::string& current_value) {
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

    if (source.has_value() and !this->storage.update_monitoring_source(id, source)) {
        EVLOG_warning << "Could not record the source of monitor: " << id;
    }

    this->forget(id);
    this->insert(StoredMonitor{request.component, request.variable, source, monitor_meta.value()});
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

void MonitorStore::reconcile(const std::string& source, const Component& component,
                             const RegisteredVariable& registered_variable,
                             const std::optional<std::string>& actual_value, const bool overrides_component_config,
                             const MonitorValidator& fits, const MonitorValidator& is_valid) {
    const auto& variable = registered_variable.variable;
    const auto ids = [this, &component, &variable]() {
        const auto it = this->by_variable.find({component, variable});
        return it == this->by_variable.end() ? std::set<std::int32_t>{} : it->second;
    };

    // N04: a monitor that no longer fits the variable is removed
    for (const auto id : ids()) {
        const auto& monitor = this->monitors.at(id).monitor.monitor;
        if (fits(monitor.type, monitor.value)) {
            continue;
        }
        EVLOG_warning << "Removing monitor " << id << " of " << component << "." << variable
                      << ": it does not fit the variable as source " << source << " registers it";
        this->remove(id);
    }

    // Hardwired and preconfigured monitors come with the registration: those it no longer lists are removed. The
    // monitors of an overridden component config variable come from the component config, which keeps them in sync.
    for (const auto id : ids()) {
        const auto& monitor = this->monitors.at(id).monitor;
        const bool listed = std::any_of(registered_variable.monitors.begin(), registered_variable.monitors.end(),
                                        [&monitor](const VariableMonitoringMeta& registered) {
                                            return registered.monitor.type == monitor.monitor.type and
                                                   registered.monitor.severity == monitor.monitor.severity;
                                        });
        if (monitor.type == VariableMonitorType::CustomMonitor or listed or overrides_component_config) {
            continue;
        }
        EVLOG_warning << "Removing monitor " << id << " of " << component << "." << variable << ": source " << source
                      << " no longer registers it";
        this->remove(id);
    }

    for (const auto& registered_monitor : registered_variable.monitors) {
        const auto& monitor = registered_monitor.monitor;
        // N04.FR.10: the name key, type and severity identify a monitor
        const auto existing = ids();
        const bool matching = std::any_of(existing.begin(), existing.end(), [this, &monitor](const std::int32_t id) {
            const auto& stored = this->monitors.at(id).monitor.monitor;
            return stored.type == monitor.type and stored.severity == monitor.severity;
        });
        if (matching) {
            continue;
        }
        if (registered_monitor.type == VariableMonitorType::CustomMonitor or !is_valid(monitor.type, monitor.value)) {
            EVLOG_warning << "Source " << source << " registered an invalid monitor for " << component << "."
                          << variable << ": the monitor is ignored";
            continue;
        }
        SetMonitoringData request;
        request.component = component;
        request.variable = variable;
        request.type = monitor.type;
        request.value = monitor.value;
        request.severity = monitor.severity;
        request.transaction = monitor.transaction;
        try {
            auto stored = this->storage.set_monitoring_data(request, registered_monitor.type);
            if (!stored.has_value()) {
                EVLOG_warning << "Could not store a monitor that source " << source << " registered for " << component
                              << "." << variable;
                continue;
            }
            if (monitor.type == MonitorEnum::Delta) {
                const auto reference = actual_value.value_or("");
                if (this->storage.update_monitoring_reference(stored->monitor.id, reference)) {
                    stored->reference_value = reference;
                }
            }
            this->insert(StoredMonitor{component, variable, std::nullopt, std::move(stored.value())});
        } catch (const everest::db::Exception& e) {
            EVLOG_error << "Could not store a monitor that source " << source << " registered: " << e.what();
        }
    }

    for (const auto id : ids()) {
        this->record_source(id, source);
    }
}

void MonitorStore::remove_of_vanished_variables(const std::string& source,
                                                const std::function<bool(const Component&, const Variable&)>& exists) {
    std::vector<std::int32_t> vanished;
    for (const auto& [id, stored] : this->monitors) {
        if (stored.source == source and !exists(stored.component, stored.variable)) {
            vanished.push_back(id);
        }
    }
    for (const auto id : vanished) {
        const auto& stored = this->monitors.at(id);
        // N04: the source registered without a variable it had before, so the variable no longer exists
        EVLOG_warning << "Removing monitor " << id << " of " << stored.component << "." << stored.variable
                      << ": source " << source << " no longer registers the variable";
        this->remove(id);
    }
}

void MonitorStore::insert(StoredMonitor monitor) {
    const auto id = monitor.monitor.monitor.id;
    this->by_variable[{monitor.component, monitor.variable}].insert(id);
    this->monitors.insert_or_assign(id, std::move(monitor));
}

void MonitorStore::remove(const std::int32_t monitor_id) {
    this->forget(monitor_id);
    try {
        if (this->storage.clear_variable_monitor(monitor_id, true) != ClearMonitoringStatusEnum::Accepted) {
            EVLOG_warning << "Could not remove monitor " << monitor_id << " from the storage";
        }
    } catch (const everest::db::Exception& e) {
        EVLOG_error << "Could not remove monitor " << monitor_id << " from the storage: " << e.what();
    }
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

void MonitorStore::record_source(const std::int32_t monitor_id, const std::optional<std::string>& source) {
    auto& stored = this->monitors.at(monitor_id);
    if (stored.source == source) {
        return;
    }
    if (!this->storage.update_monitoring_source(monitor_id, source)) {
        EVLOG_warning << "Could not record the source of monitor: " << monitor_id;
    }
    stored.source = source;
}

} // namespace v2
} // namespace ocpp
