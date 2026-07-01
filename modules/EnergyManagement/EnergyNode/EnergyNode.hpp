// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef ENERGY_NODE_HPP
#define ENERGY_NODE_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 2
//

#include "ld-ev.hpp"

// headers for provided interface implementations
#include <generated/interfaces/energy/Implementation.hpp>
#include <generated/interfaces/external_energy_limits/Implementation.hpp>

// headers for required interface implementations
#include <generated/interfaces/energy/Interface.hpp>
#include <generated/interfaces/energy_price_information/Interface.hpp>
#include <generated/interfaces/powermeter/Interface.hpp>

// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1
// insert your custom include headers here
#include <sigslot/signal.hpp>
#include <sstream>
#include <string>
#include <vector>
// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1

namespace module {

namespace energy_grid {
class energyImpl;
} // namespace energy_grid

struct Conf {
    double fuse_limit_A;
    int phase_count;
    double nominal_voltage_V;
    std::string external_consumer_ids; // comma-separated, e.g. "cluster_m0,cluster_m1"
    bool enhance_external_schedule;
};

class EnergyNode : public Everest::ModuleBase {
public:
    EnergyNode() = delete;
    // NOTE: MqttProvider& mqtt_provider is injected by the loader when
    //       enable_external_mqtt: true is set in the manifest. Re-run ev-cli
    //       code generation after adding that flag to get the updated ld-ev.cpp.
    EnergyNode(const ModuleInfo& info, Everest::MqttProvider& mqtt_provider,
               std::unique_ptr<energyImplBase> p_energy_grid,
               std::unique_ptr<external_energy_limitsImplBase> p_external_limits,
               std::vector<std::unique_ptr<energyIntf>> r_energy_consumer,
               std::vector<std::unique_ptr<powermeterIntf>> r_powermeter,
               std::vector<std::unique_ptr<energy_price_informationIntf>> r_price_information, Conf& config) :
        ModuleBase(info),
        mqtt(mqtt_provider),
        p_energy_grid(std::move(p_energy_grid)),
        p_external_limits(std::move(p_external_limits)),
        r_energy_consumer(std::move(r_energy_consumer)),
        r_powermeter(std::move(r_powermeter)),
        r_price_information(std::move(r_price_information)),
        config(config){};

    Everest::MqttProvider& mqtt;
    const std::unique_ptr<energyImplBase> p_energy_grid;
    const std::unique_ptr<external_energy_limitsImplBase> p_external_limits;
    const std::vector<std::unique_ptr<energyIntf>> r_energy_consumer;
    const std::vector<std::unique_ptr<powermeterIntf>> r_powermeter;
    const std::vector<std::unique_ptr<energy_price_informationIntf>> r_price_information;
    const Conf& config;

    // ev@1fce4c5e-0ab8-41bb-90f7-14277703d2ac:v1
    // insert your public definitions here
    sigslot::signal<types::energy::ExternalLimits&> signalExternalLimit;
    // ev@1fce4c5e-0ab8-41bb-90f7-14277703d2ac:v1

protected:
    // ev@4714b2ab-a24f-4b95-ab81-36439e1478de:v1
    // insert your protected definitions here
    // ev@4714b2ab-a24f-4b95-ab81-36439e1478de:v1

private:
    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1

    friend class LdEverest;
    friend class energy_grid::energyImpl;
    void init();
    void ready();

    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
    // Parsed list of external consumer IDs from config.external_consumer_ids
    std::vector<std::string> external_consumers;
    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
};

// ev@087e516b-124c-48df-94fb-109508c7cda9:v1
// insert other definitions here
// ev@087e516b-124c-48df-94fb-109508c7cda9:v1

} // namespace module

#endif // ENERGY_NODE_HPP
