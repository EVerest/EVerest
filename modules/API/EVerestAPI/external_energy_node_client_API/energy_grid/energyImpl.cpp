// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "energyImpl.hpp"

#include <optional>
#include <string>

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

namespace {

// Energy-tree uuids are EVerest module ids — unique only within one process. The
// site-level process may run several clients plus local EVSEs, and identically
// provisioned remote processes will reuse ids like "cp01". Namespacing every
// remote uuid with the server_id on the way in (and stripping it on the way out)
// keeps the trees disjoint, so a limit computed for THIS server's "cp01" can
// never be applied by a local "cp01" or by another server's "cp01".
constexpr char UUID_NAMESPACE_SEPARATOR = ':';

std::string namespace_prefix(const std::string& server_id) {
    return server_id + UUID_NAMESPACE_SEPARATOR;
}

void namespace_uuids(types::energy::EnergyFlowRequest& request, const std::string& prefix) {
    request.uuid = prefix + request.uuid;
    for (auto& child : request.children) {
        namespace_uuids(child, prefix);
    }
}

// Returns the remote uuid if the given uuid is in this server's namespace,
// std::nullopt otherwise.
std::optional<std::string> strip_namespace(const std::string& uuid, const std::string& prefix) {
    if (uuid.rfind(prefix, 0) == 0) {
        return uuid.substr(prefix.size());
    }
    return std::nullopt;
}

} // namespace

void energyImpl::init() {
    // Build Topics for the remote server using the ApiHelper topic convention:
    //   everest_api/1/external_energy_node/{server_id}/e2m/energy_flow_request
    //   everest_api/1/external_energy_node/{server_id}/m2e/enforce_limits
    // This only depends on config, so do it here: ready() ordering across modules
    // is not guaranteed, and the site-level EnergyManager may call
    // handle_enforce_limits before this module's ready() has run.
    server_topics.setup(mod->config.server_id, "external_energy_node", 1);
    mod->enforce_limits_topic = server_topics.extern_to_everest("enforce_limits");
}

void energyImpl::ready() {
    // Subscribe to the server's published energy_flow_request (e2m = Everest to Machine).
    // Republish on the local Everest bus — with all uuids namespaced by server_id —
    // so the site-level EnergyNode sees the server as a normal energy_consumer child.
    const auto flow_req_topic = server_topics.everest_to_extern("energy_flow_request");
    mod->mqtt_v.subscribe(flow_req_topic, [this](const std::string& msg) {
        API_types_ext::EnergyFlowRequest val;
        if (!deserialize(msg, val)) {
            EVLOG_warning << "external_energy_node_client_API [" << mod->config.server_id
                          << "]: failed to deserialize energy_flow_request";
            return;
        }
        auto request = to_internal_api(val);
        namespace_uuids(request, namespace_prefix(mod->config.server_id));

        publish_energy_flow_request(request);
    });

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
    mod->mqtt_v.subscribe(server_heartbeat_topic,
                          [this, server_comm_check_topic, local_comm_check_topic](const std::string&) {
                              const auto payload = API_generic::serialize(true);
                              mod->mqtt_v.publish(server_comm_check_topic, payload);
                              mod->mqtt_v.publish(local_comm_check_topic, payload);
                          });
}

void energyImpl::handle_enforce_limits(types::energy::EnforcedLimits& value) {
    // The site-level EnergyManager computed enforce_limits and calls this handler.
    // Site-level EnergyNodes broadcast enforce_limits to ALL children, so limits
    // addressed to purely local nodes arrive here too. Only limits for nodes in
    // this server's namespace belong to the remote subtree: forward those with
    // the prefix stripped, drop everything else. This both prevents cross-process
    // uuid collisions and stops the all-limits fan-out across the bridge.
    auto remote_uuid = strip_namespace(value.uuid, namespace_prefix(mod->config.server_id));
    if (not remote_uuid.has_value()) {
        return;
    }
    value.uuid = std::move(*remote_uuid);

    // Serialize via the ApiHelper convention and publish to the server's m2e topic
    // (Machine to Everest = client sends command to server):
    //   everest_api/1/external_energy_node/{server_id}/m2e/enforce_limits
    try {
        using API_types_ext::serialize;
        using API_types_ext::to_external_api;
        mod->mqtt_v.publish(mod->enforce_limits_topic, serialize(to_external_api(value)));
    } catch (const std::exception& e) {
        EVLOG_warning << "external_energy_node_client_API [" << mod->config.server_id
                      << "]: failed to publish enforce_limits: " << e.what();
    }
}

} // namespace energy_grid
} // namespace module
