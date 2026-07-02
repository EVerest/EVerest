// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "external_energy_node_API.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <string_view>

#include <everest_api_types/energy/codec.hpp>
#include <everest_api_types/energy/wrapper.hpp>
#include <everest_api_types/utilities/codec.hpp>

namespace module {
namespace API_types_ext = API_types::energy;

using ev_API::deserialize;

void external_energy_node_API::init() {
    invoke_init(*p_main);
    invoke_init(*p_energy_grid);

    // Initialise aggregate UUID from module id
    aggregate.uuid = info.id;
    aggregate.node_type = types::energy::NodeType::Generic;

    // Initialise ApiHelper — registers heartbeat and communication-check parameters.
    API_types_entry::CommunicationParameters comm_params{};
    comm_params.heartbeat_period_ms = config.cfg_heartbeat_interval_ms;
    comm_params.communication_check_period_s = config.cfg_communication_check_to_s;
    helper.init(comm_params);

    // Subscribe to energy_flow_request from each local EnergyNode (EVSE nodes).
    // On each update: merge the child, check external timeout, republish aggregate on
    // the local Everest bus (for internal) and via ApiHelper topic (for external via bridge).
    for (auto& entry : r_energy_consumer) {
        entry->subscribe_energy_flow_request([this](types::energy::EnergyFlowRequest const& child) {
            std::lock_guard<std::mutex> lock(aggregate_mutex);

            auto& children = aggregate.children;
            auto it = std::find_if(children.begin(), children.end(),
                                   [&child](const auto& c) {
                                       return std::string_view{c.uuid} == std::string_view{child.uuid};
                                   });
            if (it != children.end()) {
                *it = child;
            } else {
                children.push_back(child);
            }

            // Check external timeout
            if (config.timeout_s > 0 && external_active.load()) {
                const auto elapsed =
                    std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - external_last_seen)
                        .count();
                if (elapsed >= config.timeout_s) {
                    external_active = false;
                    EVLOG_info << info.id
                               << ": external EnergyManager timed out — falling back to internal EnergyManager";
                }
            }

            // Publish to local Everest bus (internal EnergyManager sees the aggregate)
            p_energy_grid->publish_energy_flow_request(aggregate);

            // Publish to external via ApiHelper topic:
            //   everest_api/1/external_energy_node/{id}/e2m/energy_flow_request
            // EnergyFlowRequest has no API-type wrapper, so we use EVerest's native JSON.
            try {
                const auto topic = helper.get_topics().everest_to_extern("energy_flow_request");
                mqtt_v.publish(topic, nlohmann::json(aggregate).dump());
            } catch (const std::exception& e) {
                EVLOG_warning << info.id << ": failed to publish energy_flow_request: " << e.what();
            }
        });
    }
}

void external_energy_node_API::ready() {
    invoke_ready(*p_main);
    invoke_ready(*p_energy_grid);

    // Publish an initial aggregate so both internal and external see this server immediately.
    {
        std::lock_guard<std::mutex> lock(aggregate_mutex);
        p_energy_grid->publish_energy_flow_request(aggregate);
        try {
            const auto topic = helper.get_topics().everest_to_extern("energy_flow_request");
            mqtt_v.publish(topic, nlohmann::json(aggregate).dump());
        } catch (const std::exception& e) {
            EVLOG_warning << info.id << ": failed to publish initial energy_flow_request: " << e.what();
        }
    }

    // Subscribe to enforce_limits commands from the external EnergyManager.
    // Arrives via ApiHelper topic:
    //   everest_api/1/external_energy_node/{id}/m2e/enforce_limits
    // EnforcedLimits has an API-type wrapper, so we use deserialize + to_internal_api.
    helper.subscribe_api_topic("enforce_limits", [this](std::string const& data) {
        API_types_ext::EnforcedLimits val;
        if (!deserialize(data, val)) {
            EVLOG_warning << info.id << ": failed to deserialize enforce_limits from external";
            return false;
        }

        auto value = to_internal_api(val);

        external_last_seen = std::chrono::steady_clock::now();
        if (!external_active.exchange(true)) {
            EVLOG_info << info.id << ": external EnergyManager connected";
        }

        // Route external limits to all child EnergyNodes (takes priority over internal).
        for (auto& entry : r_energy_consumer) {
            entry->call_enforce_limits(value);
        }
        return true;
    });

    helper.generate_api_var_communication_check(&comm_check);

    comm_check.start(config.cfg_communication_check_to_s);
    helper.setup_heartbeat_generator(&comm_check, config.cfg_heartbeat_interval_ms);

    helper.publish_ready_beacon();
}

} // namespace module
