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
#include <generated/interfaces/energy/Implementation.hpp>
#include <generated/interfaces/generic_error/Implementation.hpp>

// headers for required interface implementations
#include <generated/interfaces/energy/Interface.hpp>

// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1
// insert your custom include headers here
#include <atomic>
#include <chrono>
#include <string>
#include <vector>

#include <everest/timer.hpp>
#include <everest/util/async/monitor.hpp>
#include <everest_api_module_helpers/ApiHelper.hpp>
#include <everest_api_types/entrypoint/API.hpp>

namespace ev_API = everest::lib::API;
namespace API_types = ev_API::V1_0::types;
namespace API_types_entry = API_types::entrypoint;
// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1

namespace module {

struct Conf {
    int timeout_s;
    double fuse_limit_A;
    int phase_count;
    int cfg_heartbeat_interval_ms;
    int cfg_communication_check_to_s;
};

class external_energy_node_API : public Everest::ModuleBase {
public:
    external_energy_node_API() = delete;
    external_energy_node_API(const ModuleInfo& info, Everest::MqttProvider& mqtt_provider,
                             std::unique_ptr<generic_errorImplBase> p_main,
                             std::unique_ptr<energyImplBase> p_energy_grid,
                             std::vector<std::unique_ptr<energyIntf>> r_energy_consumer, Conf& config) :
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

    // Watchdog: (re)armed on every enforce_limits received from the external EnergyManager.
    // Fires once if timeout_s passes without a fresh message, independent of any other event.
    Everest::SteadyTimer external_timeout_timer;

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

    void generate_api_var_energy_flow_request();
    void generate_api_cmd_enforce_limits();

    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
    everest::lib::util::monitor<types::energy::EnergyFlowRequest> aggregate;

    // One-entry schedule advertising the configured local fuse limit, or an
    // entry without any limits (= unlimited pass-through) when fuse_limit_A == 0.
    // An EMPTY schedule would mean "nothing available" to the EnergyManager
    // optimizer (see Market.cpp: zero_schedule_req), clamping every EVSE below
    // this node to 0 A.
    std::vector<types::energy::ScheduleReqEntry> get_local_schedule() const;

    // Backstop: lower limits from the external EnergyManager to the configured
    // local fuse limit before forwarding them to children. Only lowers values
    // that are present — absent limits stay absent (EvseManager treats a missing
    // ac_max_current_A as 0 A, so filling them in would RAISE the limit).
    void clamp_to_local_limits(types::energy::EnforcedLimits& value) const;

    ev_API::CommCheckHandler<generic_errorImplBase> comm_check{"generic/CommunicationFault",
                                                               ev_API::bridge_connection_lost_message, p_main};
    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
};

// ev@087e516b-124c-48df-94fb-109508c7cda9:v1
// insert other definitions here
// ev@087e516b-124c-48df-94fb-109508c7cda9:v1

} // namespace module

#endif // EXTERNAL_ENERGY_NODE_API_HPP
