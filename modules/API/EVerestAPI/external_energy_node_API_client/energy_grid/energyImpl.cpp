// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "energyImpl.hpp"

#include <nlohmann/json.hpp>

#include <everest_api_types/energy/codec.hpp>
#include <everest_api_types/energy/wrapper.hpp>
#include <everest_api_types/utilities/Topics.hpp>

namespace module {
namespace energy_grid {

namespace ev_API = everest::lib::API;
namespace API_types_ext = ev_API::V1_0::types::energy;

void energyImpl::init() {
    // Nothing to do at init — MQTT subscriptions require the framework to be ready.
}

void energyImpl::ready() {
    // Build Topics for the remote server using the ApiHelper topic convention:
    //   everest_api/1/external_energy_node/{server_id}/e2m/energy_flow_request
    //   everest_api/1/external_energy_node/{server_id}/m2e/enforce_limits
    ev_API::Topics server_topics;
    server_topics.setup(mod->config.server_id, "external_energy_node", 1);

    // Subscribe to the server's published energy_flow_request (e2m = Everest to Machine).
    // Republish on the local Everest bus so the site-level EnergyNode sees the cluster
    // as a normal energy_consumer child.
    // EnergyFlowRequest has no API-type wrapper, so we use EVerest's native JSON.
    const auto flow_req_topic = server_topics.everest_to_extern("energy_flow_request");
    mod->mqtt.subscribe(flow_req_topic, [this](const std::string& msg) {
        try {
            auto flow = nlohmann::json::parse(msg).get<types::energy::EnergyFlowRequest>();
            publish_energy_flow_request(flow);
        } catch (const std::exception& e) {
            EVLOG_warning << "external_energy_node_API_client [" << mod->config.server_id
                          << "]: failed to parse energy_flow_request: " << e.what();
        }
    });

    // Store the enforce_limits topic for use in handle_enforce_limits.
    mod->enforce_limits_topic = server_topics.extern_to_everest("enforce_limits");
}

void energyImpl::handle_enforce_limits(types::energy::EnforcedLimits& value) {
    // The site-level EnergyManager computed enforce_limits and calls this handler.
    // Serialize via the ApiHelper convention and publish to the server's m2e topic
    // (Machine to Everest = client sends command to server):
    //   everest_api/1/external_energy_node/{server_id}/m2e/enforce_limits
    try {
        using API_types_ext::serialize;
        using API_types_ext::to_external_api;
        mod->mqtt.publish(mod->enforce_limits_topic,
                            serialize(to_external_api(value)));
    } catch (const std::exception& e) {
        EVLOG_warning << "external_energy_node_API_client [" << mod->config.server_id
                      << "]: failed to publish enforce_limits: " << e.what();
    }
}

} // namespace energy_grid
} // namespace module
