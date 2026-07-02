// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef EXTERNAL_ENERGY_NODE_API_HPP
#define EXTERNAL_ENERGY_NODE_API_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 2
//

#include "ld-ev.hpp"

// headers for provided interface implementations
#include <generated/interfaces/generic_error/Implementation.hpp>
#include <generated/interfaces/energy/Implementation.hpp>

// headers for required interface implementations
#include <generated/interfaces/energy/Interface.hpp>

// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1
// insert your custom include headers here
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

#include <everest_api_module_helpers/ApiHelper.hpp>
#include <everest_api_types/entrypoint/API.hpp>

namespace ev_API = everest::lib::API;
namespace API_types = ev_API::V1_0::types;
namespace API_types_entry = API_types::entrypoint;
// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1

namespace module {

struct Conf {
    int timeout_s;
    int cfg_heartbeat_interval_ms;
    int cfg_communication_check_to_s;
};

class external_energy_node_API : public Everest::ModuleBase {
public:
    external_energy_node_API() = delete;
    external_energy_node_API(const ModuleInfo& info, Everest::MqttProvider& mqtt_provider,
                             std::unique_ptr<generic_errorImplBase> p_main,
                             std::unique_ptr<energyImplBase> p_energy_grid,
                             std::vector<std::unique_ptr<energyIntf>> r_energy_consumer,
                             Conf& config) :
        ModuleBase(info),
        mqtt(mqtt_provider),
        p_main(std::move(p_main)),
        p_energy_grid(std::move(p_energy_grid)),
        r_energy_consumer(std::move(r_energy_consumer)),
        config(config){};

    Everest::MqttProvider& mqtt;
    const std::unique_ptr<generic_errorImplBase> p_main;
    const std::unique_ptr<energyImplBase> p_energy_grid;
    const std::vector<std::unique_ptr<energyIntf>> r_energy_consumer;
    const Conf& config;

    // ev@1fce4c5e-0ab8-41bb-90f7-14277703d2ac:v1
    // Shared state — written by MQTT path, read by energy_grid impl (internal path).
    std::atomic<bool> external_active{false};
    std::chrono::steady_clock::time_point external_last_seen{};

    ev_API::Mqtt::ValidatingMqttProxy mqtt_v{mqtt};
    ev_API::ApiHelper helper{info, mqtt_v, {{"external_energy_node", 1}}, get_config_service_client()};
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
    std::mutex aggregate_mutex;
    types::energy::EnergyFlowRequest aggregate;

    ev_API::CommCheckHandler<generic_errorImplBase> comm_check{"generic/CommunicationFault",
                                                               ev_API::bridge_connection_lost_message,
                                                               p_main};
    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
};

// ev@087e516b-124c-48df-94fb-109508c7cda9:v1
// insert other definitions here
// ev@087e516b-124c-48df-94fb-109508c7cda9:v1

} // namespace module

#endif // EXTERNAL_ENERGY_NODE_API_HPP
