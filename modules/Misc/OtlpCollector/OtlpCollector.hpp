// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef OTLP_COLLECTOR_HPP
#define OTLP_COLLECTOR_HPP

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
// insert your custom include headers here
#include <memory>
#include <vector>

#include "opentelemetry/proto/collector/metrics/v1/metrics_service.pb.h"

#include "OtlpHttpServer.hpp"
// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1

namespace module {

struct Conf {
    int listen_port;
    std::string component_name;
};

class OtlpCollector : public Everest::ModuleBase {
public:
    OtlpCollector() = delete;
    OtlpCollector(const ModuleInfo& info, std::unique_ptr<emptyImplBase> p_main,
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
    // insert your private definitions here
    /// Receiver callback: decodes the OTLP payload and writes it to the device model
    bool on_metrics(const std::string& payload);
    /// Maps every gauge and sum data point to a device model variable write
    std::vector<types::ocpp::SetVariableRequest> to_set_variable_requests(
        const opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceRequest& request) const;

    std::unique_ptr<OtlpHttpServer> server;
    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
};

// ev@087e516b-124c-48df-94fb-109508c7cda9:v1
// insert other definitions here
// ev@087e516b-124c-48df-94fb-109508c7cda9:v1

} // namespace module

#endif // OTLP_COLLECTOR_HPP
