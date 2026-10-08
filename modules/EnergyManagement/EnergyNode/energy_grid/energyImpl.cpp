// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "energyImpl.hpp"
#include "energy_schedule_utils.hpp"
#include <everest/helpers/phase_rotation.hpp>

#include <algorithm>
#include <chrono>
#include <date/date.h>
#include <date/tz.h>
#include <string>
#include <string_view>
#include <utils/date.hpp>
#include <vector>

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

    for (auto const& consumer : mod->r_energy_consumer) {
        consumer->subscribe_energy_flow_request(
            [this, connection = consumer.get()](auto const& e) { update_child_energy_flow_request(connection, e); });
    }

    if (!mod->r_powermeter.empty()) {
        mod->r_powermeter[0]->subscribe_powermeter([this](types::powermeter::Powermeter const& p) {
            EVLOG_debug << "Incoming powermeter readings: " << p;

            auto energy_state_handle = energy_state.handle();
            const auto phase_rotation = everest::helpers::phase_rotation_from_string(mod->config.phase_rotation);

            energy_state_handle->energy_flow_request.energy_usage_root =
                everest::helpers::apply_phase_rotation(p, phase_rotation);

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

void energyImpl::update_child_energy_flow_request(energyIntf* connection,
                                                  const types::energy::EnergyFlowRequest& request) {
    // Received new energy_flow_request object from a child. Update in the cached object and republish.
    auto energy_state_handle = energy_state.handle();

    auto& children = energy_state_handle->energy_flow_request.children;
    auto children_it = std::find_if(children.begin(), children.end(), [&request](auto const& child) {
        return std::string_view{child.uuid} == std::string_view{request.uuid};
    });
    if (children_it != children.end()) {
        *children_it = request;
    } else {
        children.push_back(request);
    }
    energy_state_handle->child_connections[request.uuid] = connection;

    publish_complete_energy_object(*energy_state_handle);
}

std::vector<energyIntf*> energyImpl::find_target_connections(const EnergyState& state, std::string_view uuid) {
    std::vector<energyIntf*> connections;

    for (auto const& child : state.energy_flow_request.children) {
        if (energy_flow_request_contains_uuid(child, uuid)) {
            connections.push_back(state.child_connections.at(child.uuid));
        }
    }

    return connections;
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
    } else {
        publish_energy_flow_request(energy_flow_request);
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
}

void energyImpl::handle_enforce_limits(types::energy::EnforcedLimits& value) {
    std::vector<energyIntf*> target_connections;

    {
        auto energy_state_handle = energy_state.handle();

        // it is for this node itself, no need to route it to children
        if (value.uuid == energy_state_handle->energy_flow_request.uuid) {
            return;
        }

        target_connections = find_target_connections(*energy_state_handle, value.uuid);
    }

    if (target_connections.empty()) {
        // Unknown uuid (e.g. no energy_flow_request received from that child yet): send to all children
        for (auto const& consumer : mod->r_energy_consumer) {
            target_connections.push_back(consumer.get());
        }
    }

    for (auto const connection : target_connections) {
        connection->call_enforce_limits(value);
    }
};

} // namespace energy_grid
} // namespace module
