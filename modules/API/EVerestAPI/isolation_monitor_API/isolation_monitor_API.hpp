// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#ifndef ISOLATION_MONITOR_API_HPP
#define ISOLATION_MONITOR_API_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 2
//

#include "ld-ev.hpp"

// headers for provided interface implementations
#include <generated/interfaces/isolation_monitor/Implementation.hpp>

// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1
// insert your custom include headers here
#include <everest_api_module_helpers/ApiHelper.hpp>
#include <everest_api_types/entrypoint/API.hpp>
#include <everest_api_types/isolation_monitor/API.hpp>

namespace ev_API = everest::lib::API;
namespace API_types = ev_API::V1_0::types;
namespace API_types_entry = API_types::entrypoint;
namespace API_types_ext = API_types::isolation_monitor;
// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1

namespace module {

struct RwConf {};

struct Conf {
    int cfg_communication_check_to_s;
    int cfg_heartbeat_interval_ms;

    Conf() = default;
    Conf(const RwConf&){};
};

class isolation_monitor_API : public Everest::ModuleBase {
public:
    isolation_monitor_API() = delete;
    isolation_monitor_API(const ModuleInfo& info, Everest::MqttProvider& mqtt_provider,
                          std::unique_ptr<isolation_monitorImplBase> p_main, Conf& config) :
        ModuleBase(info), mqtt(mqtt_provider), p_main(std::move(p_main)), config(config){};

    Everest::MqttProvider& mqtt;
    const std::unique_ptr<isolation_monitorImplBase> p_main;
    const Conf& config;

    // ev@1fce4c5e-0ab8-41bb-90f7-14277703d2ac:v1
    // insert your public definitions here
    ev_API::Mqtt::ValidatingMqttProxy mqtt_v{mqtt};
    ev_API::ApiHelper helper{info, mqtt_v, {{"isolation_monitor", 1}}, get_config_service_client()};
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
    // insert your private definitions here
    void generate_api_var_isolation_measurement();
    void generate_api_var_self_test_result();
    void generate_api_var_raise_error();
    void generate_api_var_clear_error();

    std::string make_error_string(API_types_ext::Error const& error);

    ev_API::CommCheckHandler<isolation_monitorImplBase> comm_check{"isolation_monitor/CommunicationFault",
                                                                   ev_API::bridge_connection_lost_message, p_main};
    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
};

// ev@087e516b-124c-48df-94fb-109508c7cda9:v1
// insert other definitions here
// ev@087e516b-124c-48df-94fb-109508c7cda9:v1

} // namespace module

#endif // ISOLATION_MONITOR_API_HPP
