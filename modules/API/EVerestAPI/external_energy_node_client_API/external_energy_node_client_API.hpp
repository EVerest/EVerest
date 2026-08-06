// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef EXTERNAL_ENERGY_NODE_CLIENT_API_HPP
#define EXTERNAL_ENERGY_NODE_CLIENT_API_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 2
//

#include "ld-ev.hpp"

// headers for provided interface implementations
#include <generated/interfaces/energy/Implementation.hpp>
#include <generated/interfaces/generic_error/Implementation.hpp>

// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1
// insert your custom include headers here
#include <chrono>
#include <mutex>
#include <string>

#include <everest/timer.hpp>
#include <everest_api_module_helpers/ApiHelper.hpp>
#include <everest_api_types/entrypoint/API.hpp>

namespace ev_API = everest::lib::API;
namespace API_types = ev_API::V1_0::types;
namespace API_types_entry = API_types::entrypoint;
// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1

namespace module {

struct Conf {
    std::string server_id;
    int stale_timeout_s;
    int cfg_heartbeat_interval_ms;
    int cfg_communication_check_to_s;
};

class external_energy_node_client_API : public Everest::ModuleBase {
public:
    external_energy_node_client_API() = delete;
    // NOTE: MqttProvider& is injected by the generated ld-ev.cpp because
    //       enable_external_mqtt: true is set in the manifest.
    external_energy_node_client_API(const ModuleInfo& info, Everest::MqttProvider& mqtt_provider,
                                    std::unique_ptr<generic_errorImplBase> p_main,
                                    std::unique_ptr<energyImplBase> p_energy_grid, Conf& config) :
        ModuleBase(info),
        mqtt(mqtt_provider),
        p_main(std::move(p_main)),
        p_energy_grid(std::move(p_energy_grid)),
        config(config){};

    Everest::MqttProvider& mqtt;
    const std::unique_ptr<generic_errorImplBase> p_main;
    const std::unique_ptr<energyImplBase> p_energy_grid;
    const Conf& config;

    // ev@1fce4c5e-0ab8-41bb-90f7-14277703d2ac:v1
    // Cached enforce_limits topic — set during init(), used in handle_enforce_limits.
    std::string enforce_limits_topic;

    // Record a fresh energy_flow_request from the remote server: feeds the
    // staleness watchdog. uuid is the (namespaced) aggregate root uuid.
    void note_remote_request(const std::string& uuid);

    ev_API::Mqtt::ValidatingMqttProxy mqtt_v{mqtt};
    ev_API::ApiHelper helper{info, mqtt_v, {{"external_energy_node_client", 1}}, get_config_service_client()};
    // ev@1fce4c5e-0ab8-41bb-90f7-14277703d2ac:v1

protected:
    // ev@4714b2ab-a24f-4b95-ab81-36439e1478de:v1
    // insert your protected definitions here
    // ev@4714b2ab-a24f-4b95-ab81-36439e1478de:v1

private:
    friend class LdEverest;
    void init();
    void ready();

    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
    // Staleness watchdog: (re)armed on every energy_flow_request from the remote
    // server. The timer is only the scheduling mechanism — the decision is made
    // against stale_deadline (monotonic steady_clock) inside on_remote_stale(),
    // which makes it immune to Everest::Timer's queued-handler re-arm race and
    // to NTP steps of its date::utc_clock.
    Everest::SteadyTimer stale_timer;

    // Timer callback: withdraws the remote subtree (publishes a zero-limit
    // aggregate) so the site-level EnergyManager stops reserving budget for
    // EVSEs it can no longer reach. Re-arms on a stale/premature fire.
    void on_remote_stale();

    // Guards the staleness state below.
    std::mutex stale_mutex;
    // Monotonic deadline after which the remote aggregate counts as stale.
    std::chrono::steady_clock::time_point stale_deadline{std::chrono::steady_clock::time_point::min()};
    std::string last_remote_uuid;
    bool remote_stale_reported{false};

    ev_API::CommCheckHandler<generic_errorImplBase> comm_check{"generic/CommunicationFault",
                                                               ev_API::bridge_connection_lost_message, p_main};
    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
};

// ev@087e516b-124c-48df-94fb-109508c7cda9:v1
// insert other definitions here
// ev@087e516b-124c-48df-94fb-109508c7cda9:v1

} // namespace module

#endif // EXTERNAL_ENERGY_NODE_CLIENT_API_HPP
