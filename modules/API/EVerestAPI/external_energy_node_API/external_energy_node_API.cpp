// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "external_energy_node_API.hpp"

#include <algorithm>
#include <string_view>

#include <utils/date.hpp>

#include <everest_api_types/energy/codec.hpp>
#include <everest_api_types/energy/wrapper.hpp>
#include <everest_api_types/utilities/codec.hpp>

namespace module {
namespace API_types_ext = API_types::energy;

using API_types_ext::serialize;
using API_types_ext::to_external_api;
using ev_API::deserialize;

void external_energy_node_API::init() {
    invoke_init(*p_main);
    invoke_init(*p_energy_grid);

    // Initialise aggregate from module id
    {
        auto agg = aggregate.handle();
        agg->uuid = info.id;
        agg->node_type = types::energy::NodeType::Generic;

        // Always seed a one-entry schedule: an empty schedule_import/export is
        // interpreted by the EnergyManager optimizer as "nothing available"
        // (Market.cpp: zero_schedule_req) and would clamp every EVSE below this
        // node to 0 A. The entry carries the configured local fuse limit, or no
        // limits at all (= unlimited pass-through) when fuse_limit_A == 0.
        agg->schedule_import = get_local_schedule();
        agg->schedule_export = get_local_schedule();
    }

    // Initialise ApiHelper — registers heartbeat and communication-check parameters.
    API_types_entry::CommunicationParameters comm_params{};
    comm_params.heartbeat_period_ms = config.cfg_heartbeat_interval_ms;
    comm_params.communication_check_period_s = config.cfg_communication_check_to_s;
    helper.init(comm_params);
}

void external_energy_node_API::ready() {
    invoke_ready(*p_main);
    invoke_ready(*p_energy_grid);

    generate_api_var_energy_flow_request();
    generate_api_cmd_enforce_limits();

    // Publish an initial aggregate so both internal and external see this server immediately.
    {
        auto agg = aggregate.handle();
        p_energy_grid->publish_energy_flow_request(*agg);
        try {
            const auto topic = helper.get_topics().everest_to_extern("energy_flow_request");
            mqtt_v.publish(topic, serialize(to_external_api(*agg)));
        } catch (const std::exception& e) {
            EVLOG_warning << info.id << ": failed to publish initial energy_flow_request: " << e.what();
        }
    }

    helper.generate_api_var_communication_check(&comm_check);

    comm_check.start(config.cfg_communication_check_to_s);
    helper.setup_heartbeat_generator(&comm_check, config.cfg_heartbeat_interval_ms);

    helper.publish_ready_beacon();
}

std::vector<types::energy::ScheduleReqEntry> external_energy_node_API::get_local_schedule() const {
    types::energy::ScheduleReqEntry entry;
    entry.timestamp = Everest::Date::to_rfc3339(date::utc_clock::now());

    if (config.fuse_limit_A > 0.) {
        const std::string source = info.id + "/module_config";
        entry.limits_to_root.ac_max_current_A = {static_cast<float>(config.fuse_limit_A), source};
        entry.limits_to_root.ac_max_phase_count = {config.phase_count, source};
        entry.limits_to_leaves.ac_max_current_A = {static_cast<float>(config.fuse_limit_A), source};
        entry.limits_to_leaves.ac_max_phase_count = {config.phase_count, source};
    }
    // fuse_limit_A == 0: leave limits_to_root / limits_to_leaves unset => unlimited pass-through

    return {entry};
}

void external_energy_node_API::clamp_to_local_limits(types::energy::EnforcedLimits& value) const {
    if (config.fuse_limit_A <= 0.) {
        return;
    }

    const auto fuse_limit = static_cast<float>(config.fuse_limit_A);
    const std::string source = info.id + "/module_config";

    const auto clamp = [&](types::energy::LimitsRes& limits) {
        auto& current = limits.ac_max_current_A;
        if (current.has_value() and current->value > fuse_limit) {
            current = {fuse_limit, source};
        }
        auto& phases = limits.ac_max_phase_count;
        if (phases.has_value() and phases->value > config.phase_count) {
            phases = {config.phase_count, source};
        }
    };

    clamp(value.limits_root_side);
    for (auto& entry : value.schedule) {
        clamp(entry.limits_to_root);
    }
}

void external_energy_node_API::generate_api_var_energy_flow_request() {
    // Subscribe to energy_flow_request from each local EnergyNode (EVSE nodes).
    // On each update: merge the child, republish aggregate on the local Everest bus
    // (for internal) and via ApiHelper topic (for external via bridge). The external
    // timeout is handled independently by external_timeout_timer, not here.
    for (auto& entry : r_energy_consumer) {
        entry->subscribe_energy_flow_request([this](types::energy::EnergyFlowRequest const& child) {
            auto agg = aggregate.handle();

            auto& children = agg->children;
            auto it = std::find_if(children.begin(), children.end(), [&child](const auto& c) {
                return std::string_view{c.uuid} == std::string_view{child.uuid};
            });
            if (it != children.end()) {
                *it = child;
            } else {
                children.push_back(child);
            }

            // Publish to local Everest bus (internal EnergyManager sees the aggregate)
            p_energy_grid->publish_energy_flow_request(*agg);

            // Publish to external via ApiHelper topic:
            //   everest_api/1/external_energy_node/{id}/e2m/energy_flow_request
            try {
                const auto topic = helper.get_topics().everest_to_extern("energy_flow_request");
                mqtt_v.publish(topic, serialize(to_external_api(*agg)));
            } catch (const std::exception& e) {
                EVLOG_warning << info.id << ": failed to publish energy_flow_request: " << e.what();
            }
        });
    }
}

void external_energy_node_API::generate_api_cmd_enforce_limits() {
    // Subscribe to enforce_limits commands from the external EnergyManager.
    // Arrives via ApiHelper topic:
    //   everest_api/1/external_energy_node/{id}/m2e/enforce_limits
    helper.subscribe_api_topic("enforce_limits", [this](std::string const& data) {
        API_types_ext::EnforcedLimits val;
        if (!deserialize(data, val)) {
            EVLOG_warning << info.id << ": failed to deserialize enforce_limits from external";
            return false;
        }

        auto value = to_internal_api(val);

        // Backstop against a misbehaving external EnergyManager: never forward
        // more than the locally configured fuse limit. The primary enforcement is
        // the fuse limit advertised in the aggregate's schedule (get_local_schedule),
        // which a well-behaved external optimizer already respects.
        clamp_to_local_limits(value);

        if (!external_active.exchange(true)) {
            EVLOG_info << info.id << ": external EnergyManager connected";
        }

        // (Re)arm the fallback watchdog. If no further enforce_limits arrives within
        // timeout_s, this fires exactly once and falls back to internal EnergyManager —
        // independent of whether any local EnergyNode publishes anything in the meantime.
        if (config.timeout_s > 0) {
            external_timeout_timer.timeout(
                [this]() {
                    external_active = false;
                    EVLOG_info << info.id
                               << ": external EnergyManager timed out — falling back to internal EnergyManager";
                },
                std::chrono::seconds(config.timeout_s));
        }

        // Route external limits to all child EnergyNodes (takes priority over internal).
        for (auto& entry : r_energy_consumer) {
            entry->call_enforce_limits(value);
        }
        return true;
    });
}

} // namespace module
