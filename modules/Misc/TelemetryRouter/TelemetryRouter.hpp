// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#ifndef TELEMETRY_ROUTER_HPP
#define TELEMETRY_ROUTER_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 3
//

#include "ld-ev.hpp"

// headers for provided interface implementations
#include <generated/interfaces/empty/Implementation.hpp>

// headers for required interface implementations
#include <generated/interfaces/ocpp/Interface.hpp>

// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1
#include <memory>

#include "lib/ocpp_sink.hpp"
#include "lib/receiver.hpp"
#include "lib/router.hpp"
// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1

namespace module {

struct Conf {
    std::string rules_file;
    bool strict_rules;
    int socket_receive_buffer_bytes;
    int ocpp_flush_interval_ms;
    int ocpp_min_interval_s;
    int stats_log_interval_s;
    int max_dynamic_producers;
    int max_dynamic_elements;
};

class TelemetryRouter : public Everest::ModuleBase {
public:
    TelemetryRouter() = delete;
    TelemetryRouter(const ModuleInfo& info, std::unique_ptr<emptyImplBase> p_main,
                    std::vector<std::unique_ptr<ocppIntf>> r_ocpp, Conf& config) :
        ModuleBase(info), p_main(std::move(p_main)), r_ocpp(std::move(r_ocpp)), config(config){};

    const std::unique_ptr<emptyImplBase> p_main;
    const std::vector<std::unique_ptr<ocppIntf>> r_ocpp;
    const Conf& config;

    // ev@1fce4c5e-0ab8-41bb-90f7-14277703d2ac:v1
    // insert your public definitions here
    // ev@1fce4c5e-0ab8-41bb-90f7-14277703d2ac:v1

protected:
    // ev@4714b2ab-a24f-4b95-ab81-36439e1478de:v1
    // insert your protected definitions here
    // ev@4714b2ab-a24f-4b95-ab81-36439e1478de:v1

private:
    friend class LdEverest;
    void init();
    void ready();
    void shutdown();

    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
    void log_statistics();

    std::unique_ptr<telemetry_router::IOcppClient> m_ocpp_client;
    std::unique_ptr<telemetry_router::Router> m_router;
    std::unique_ptr<telemetry_router::DatagramReceiver> m_receiver;
    telemetry_router::RouterStatistics m_logged_statistics;
    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
};

// ev@087e516b-124c-48df-94fb-109508c7cda9:v1
// insert other definitions here
// ev@087e516b-124c-48df-94fb-109508c7cda9:v1

} // namespace module

#endif // TELEMETRY_ROUTER_HPP
