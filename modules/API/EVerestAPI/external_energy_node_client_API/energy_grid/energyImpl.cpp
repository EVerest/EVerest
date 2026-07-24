// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "energyImpl.hpp"

#include <everest_api_types/energy/codec.hpp>
#include <everest_api_types/energy/wrapper.hpp>
#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/utilities/Topics.hpp>

namespace module {
namespace energy_grid {

namespace ev_API = everest::lib::API;
namespace API_types_ext = ev_API::V1_0::types::energy;
namespace API_generic = ev_API::V1_0::types::generic;

using API_types_ext::to_internal_api;
using ev_API::deserialize;

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
    const auto flow_req_topic = server_topics.everest_to_extern("energy_flow_request");
    mod->mqtt.subscribe(flow_req_topic, [this](const std::string& msg) {
        API_types_ext::EnergyFlowRequest val;
        if (!deserialize(msg, val)) {
            EVLOG_warning << "external_energy_node_client_API [" << mod->config.server_id
                          << "]: failed to deserialize energy_flow_request";
            return;
        }
        publish_energy_flow_request(to_internal_api(val));
    });

    // Store the enforce_limits topic for use in handle_enforce_limits.
    mod->enforce_limits_topic = server_topics.extern_to_everest("enforce_limits");

    // Communication-check handshake.
    // Neither module publishes communication_check on its own; each side's ApiHelper only
    // raises the initial CommunicationFault and waits for a communication_check on its own
    // m2e topic to clear it. The client knows both namespaces (its own via helper, the
    // server's via server_id), so it drives both directions: whenever the server's heartbeat
    // arrives — proof the bridged link is alive — echo a communication_check to
    //   - the server's m2e topic  (clears the server's CommunicationFault, over the bridge)
    //   - this client's own m2e topic (clears this client's CommunicationFault, locally)
    const auto server_heartbeat_topic = server_topics.everest_to_extern("heartbeat");
    const auto server_comm_check_topic = server_topics.extern_to_everest("communication_check");
    const auto local_comm_check_topic = mod->helper.get_topics().extern_to_everest("communication_check");
    mod->mqtt.subscribe(server_heartbeat_topic,
                        [this, server_comm_check_topic, local_comm_check_topic](const std::string&) {
                            const auto payload = API_generic::serialize(true);
                            mod->mqtt.publish(server_comm_check_topic, payload);
                            mod->mqtt.publish(local_comm_check_topic, payload);
                        });
}

void energyImpl::handle_enforce_limits(types::energy::EnforcedLimits& value) {
    // The site-level EnergyManager computed enforce_limits and calls this handler.
    // Serialize via the ApiHelper convention and publish to the server's m2e topic
    // (Machine to Everest = client sends command to server):
    //   everest_api/1/external_energy_node/{server_id}/m2e/enforce_limits
    try {
        using API_types_ext::serialize;
        using API_types_ext::to_external_api;
        mod->mqtt.publish(mod->enforce_limits_topic, serialize(to_external_api(value)));
    } catch (const std::exception& e) {
        EVLOG_warning << "external_energy_node_client_API [" << mod->config.server_id
                      << "]: failed to publish enforce_limits: " << e.what();
    }
}

} // namespace energy_grid
} // namespace module
