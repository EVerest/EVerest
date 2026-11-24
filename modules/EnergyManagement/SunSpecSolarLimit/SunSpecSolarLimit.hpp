// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef SUN_SPEC_SOLAR_LIMIT_HPP
#define SUN_SPEC_SOLAR_LIMIT_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 2
//

#include "ld-ev.hpp"

// headers for required interface implementations
#include <generated/interfaces/external_energy_limits/Interface.hpp>

// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1
#include <memory>

#include <sunspec/limit_policy.hpp>
// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1

namespace module {

struct Conf {
    std::string inverter_host;
    int inverter_port;
    int unit_id;
    int poll_interval_ms;
    double reserve_W;
    double fallback_power_W;
    int stale_timeout_s;
};

class SunSpecSolarLimit : public Everest::ModuleBase {
public:
    SunSpecSolarLimit() = delete;
    SunSpecSolarLimit(const ModuleInfo& info, std::unique_ptr<external_energy_limitsIntf> r_energy_node, Conf& config) :
        ModuleBase(info), r_energy_node(std::move(r_energy_node)), config(config){};

    const std::unique_ptr<external_energy_limitsIntf> r_energy_node;
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

    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
    void poll_loop();
    void send_limit(double limit_W);

    std::unique_ptr<sunspec_solar::LimitPolicy> policy;
    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
};

// ev@087e516b-124c-48df-94fb-109508c7cda9:v1
// insert other definitions here
// ev@087e516b-124c-48df-94fb-109508c7cda9:v1

} // namespace module

#endif // SUN_SPEC_SOLAR_LIMIT_HPP
