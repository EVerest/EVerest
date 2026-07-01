// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "energyImpl.hpp"
#include "energy_schedule_utils.hpp"
#include <algorithm>
#include <chrono>
#include <date/date.h>
#include <date/tz.h>
#include <nlohmann/json.hpp>
#include <string_view>
#include <utils/date.hpp>

namespace module {
namespace energy_grid {

void energyImpl::init() {
    auto energy_state_handle = energy_state.handle();

    energy_state_handle->energy_flow_request.uuid = mod->info.id;
    energy_state_handle->energy_flow_request.node_type = types::energy::NodeType::Generic;

    source_cfg = mod->info.id + "/module_config";

    // Initialize with sane defaults
    energy_state_handle->energy_flow_request.schedule_import = get_local_schedule();
    energy_state_handle->energy_flow_request.schedule_export = get_local_schedule();

    for (auto& entry : mod->r_energy_consumer) {
        entry->subscribe_energy_flow_request([this](types::energy::EnergyFlowRequest const& e) {
            // Received new energy_flow_request object from a child. Update in the cached object and republish.
            auto energy_state_handle = energy_state.handle();

            auto& children = energy_state_handle->energy_flow_request.children;
            auto children_it = std::find_if(children.begin(), children.end(), [&e](const auto& child) {
                return std::string_view{child.uuid} == std::string_view{e.uuid};
            });
            if (children_it != children.end()) {
                *children_it = e;
            } else {
                children.push_back(std::move(e));
            }

            publish_complete_energy_object(*energy_state_handle);
        });
    }

    if (!mod->r_powermeter.empty()) {
        mod->r_powermeter[0]->subscribe_powermeter([this](types::powermeter::Powermeter const& p) {
            EVLOG_debug << "Incoming powermeter readings: " << p;
            auto energy_state_handle = energy_state.handle();
            energy_state_handle->energy_flow_request.energy_usage_root = p;
            publish_complete_energy_object(*energy_state_handle);
        });
    }

    if (!mod->r_price_information.empty()) {
        mod->r_price_information[0]->subscribe_energy_pricing(
            [this](types::energy_price_information::EnergyPriceSchedule p) {
                EVLOG_debug << "Incoming price schedule: " << p;
                auto energy_state_handle = energy_state.handle();
                energy_state_handle->energy_pricing = p;
                publish_complete_energy_object(*energy_state_handle);
            });
    }
}

types::energy::ScheduleReqEntry energyImpl::get_local_schedule_req_entry() {
    types::energy::ScheduleReqEntry local_schedule;
    auto tp = date::utc_clock::now();

    local_schedule.timestamp =
        Everest::Date::to_rfc3339(date::floor<std::chrono::hours>(tp) + date::get_leap_second_info(tp).elapsed);
    local_schedule.limits_to_root.ac_max_phase_count = {mod->config.phase_count, source_cfg};
    local_schedule.limits_to_root.ac_max_current_A = {static_cast<float>(mod->config.fuse_limit_A), source_cfg};
    local_schedule.limits_to_leaves.ac_max_phase_count = {mod->config.phase_count, source_cfg};
    local_schedule.limits_to_leaves.ac_max_current_A = {static_cast<float>(mod->config.fuse_limit_A), source_cfg};

    return local_schedule;
}

std::vector<types::energy::ScheduleReqEntry> energyImpl::get_local_schedule() {
    const auto local_schedule = get_local_schedule_req_entry();
    return std::vector<types::energy::ScheduleReqEntry>({local_schedule});
}

void energyImpl::set_external_limits(types::energy::ExternalLimits& l) {
    auto energy_state_handle = energy_state.handle();

    // Process import schedule
    energy_state_handle->energy_flow_request.schedule_import = l.schedule_import;
    if (not energy_state_handle->energy_flow_request.schedule_import.empty()) {
        module::energy_grid::process_schedule_with_limits(
            energy_state_handle->energy_flow_request.schedule_import, source_cfg, mod->config.fuse_limit_A,
            mod->config.phase_count, mod->config.nominal_voltage_V, mod->config.enhance_external_schedule);
    } else {
        // At least add our local config limit even if the external limit did not set an import schedule
        energy_state_handle->energy_flow_request.schedule_import = get_local_schedule();
    }

    // Process export schedule
    energy_state_handle->energy_flow_request.schedule_export = l.schedule_export;
    if (not energy_state_handle->energy_flow_request.schedule_export.empty()) {
        module::energy_grid::process_schedule_with_limits(
            energy_state_handle->energy_flow_request.schedule_export, source_cfg, mod->config.fuse_limit_A,
            mod->config.phase_count, mod->config.nominal_voltage_V, mod->config.enhance_external_schedule);
    } else {
        // At least add our local config limit even if the external limit did not set an export schedule
        energy_state_handle->energy_flow_request.schedule_export = get_local_schedule();
    }

    energy_state_handle->energy_flow_request.schedule_setpoints = l.schedule_setpoints;
}

void energyImpl::publish_complete_energy_object(const EnergyState& state) {
    // This method is always called from contexts that already hold the energy_state lock
    const auto& energy_flow_request = state.energy_flow_request;
    const auto& energy_pricing_schedule_export = state.energy_pricing.schedule_export;

    if (not energy_flow_request.schedule_export.empty() and not energy_pricing_schedule_export.empty()) {
        types::energy::EnergyFlowRequest energy_complete = energy_flow_request;
        merge_price_into_schedule(energy_complete.schedule_export, energy_pricing_schedule_export);
        publish_energy_flow_request(energy_complete);
        // Mirror to external MQTT so cross-process peers can observe this node's subtree
        if (!mod->external_consumers.empty()) {
            mod->mqtt.publish(mod->info.id + "/energy_grid/var/energy_flow_request",
                              nlohmann::json(energy_complete).dump());
        }
    } else {
        publish_energy_flow_request(energy_flow_request);
        // Mirror to external MQTT so cross-process peers can observe this node's subtree
        if (!mod->external_consumers.empty()) {
            mod->mqtt.publish(mod->info.id + "/energy_grid/var/energy_flow_request",
                              nlohmann::json(energy_flow_request).dump());
        }
    }
}

void energyImpl::merge_price_into_schedule(std::vector<types::energy::ScheduleReqEntry>& schedule,
                                           const std::vector<types::energy_price_information::PricePerkWh>& price) {
    auto it_schedule = schedule.begin();
    auto it_price = price.begin();

    std::vector<types::energy::ScheduleReqEntry> joined_schedule;

    // The first element is already valid now even if the timestamp is in the future (per agreement)
    auto next_entry_schedule = *it_schedule;
    auto next_entry_price = *it_price;
    auto currently_valid_entry_schedule = next_entry_schedule;
    auto currently_valid_entry_price = next_entry_price;

    while (it_schedule != schedule.end() && it_price != price.end()) {
        auto tp_schedule = Everest::Date::from_rfc3339(next_entry_schedule.timestamp);
        auto tp_price = Everest::Date::from_rfc3339(next_entry_price.timestamp);

        if ((tp_schedule < tp_price && it_schedule != schedule.end()) || it_price == price.end()) {
            currently_valid_entry_schedule = next_entry_schedule;
            auto joined_entry = currently_valid_entry_schedule;

            joined_entry.price_per_kwh = currently_valid_entry_price;
            joined_schedule.push_back(joined_entry);
            it_schedule++;
            if (it_schedule != schedule.end()) {
                next_entry_schedule = *it_schedule;
            }
            continue;
        }

        if ((tp_price < tp_schedule && it_price != price.end()) || it_schedule == schedule.end()) {
            currently_valid_entry_price = next_entry_price;
            auto joined_entry = currently_valid_entry_schedule;
            joined_entry.price_per_kwh = currently_valid_entry_price;
            joined_entry.timestamp = currently_valid_entry_price.timestamp;
            joined_schedule.push_back(joined_entry);
            it_price++;
            if (it_price != price.end()) {
                next_entry_price = *it_price;
            }
            continue;
        }
    }
}

void energyImpl::ready() {
    auto energy_state_handle = energy_state.handle();
    // publish own limits at least once
    publish_energy_flow_request(energy_state_handle->energy_flow_request);
    mod->signalExternalLimit.connect([this](types::energy::ExternalLimits& l) { set_external_limits(l); });

    // Subscribe to energy_flow_request from each external consumer (running in a separate Everest process).
    // When received, inject the request as a child of this node and republish the aggregated tree upstream.
    for (const auto& consumer_id : mod->external_consumers) {
        mod->mqtt.subscribe(consumer_id + "/energy_grid/var/energy_flow_request",
                            [this, consumer_id](const std::string& msg) {
                                try {
                                    auto flow_request =
                                        nlohmann::json::parse(msg).get<types::energy::EnergyFlowRequest>();
                                    auto energy_state_handle = energy_state.handle();
                                    auto& children = energy_state_handle->energy_flow_request.children;
                                    auto it = std::find_if(children.begin(), children.end(),
                                                           [&flow_request](const auto& child) {
                                                               return child.uuid == flow_request.uuid;
                                                           });
                                    if (it != children.end()) {
                                        *it = flow_request;
                                    } else {
                                        children.push_back(flow_request);
                                    }
                                    publish_complete_energy_object(*energy_state_handle);
                                } catch (const std::exception& e) {
                                    EVLOG_warning << "EnergyNode: failed to parse external energy_flow_request from "
                                                  << consumer_id << ": " << e.what();
                                }
                            });
    }
}

void energyImpl::handle_enforce_limits(types::energy::EnforcedLimits& value) {
    auto energy_state_handle = energy_state.handle();

    // route to children if it is not for me
    // FIXME: this sends it to all children, we could do a lookup on which branch it actually is
    if (value.uuid != energy_state_handle->energy_flow_request.uuid) {
        // Check if this enforce_limits targets an external consumer (cross-process routing via MQTT)
        bool routed_external = false;
        for (const auto& consumer_id : mod->external_consumers) {
            if (value.uuid == consumer_id) {
                mod->mqtt.publish(consumer_id + "/energy_grid/cmd/enforce_limits",
                                  nlohmann::json(value).dump());
                routed_external = true;
                break;
            }
        }
        // Route to local children (internal Everest bus) if not handled externally
        if (!routed_external) {
            for (auto& entry : mod->r_energy_consumer) {
                entry->call_enforce_limits(value);
            }
        }
    }
};

} // namespace energy_grid
} // namespace module
