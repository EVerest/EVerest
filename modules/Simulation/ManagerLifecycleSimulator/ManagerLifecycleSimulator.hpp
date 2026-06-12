// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef MANAGERLIFECYCLESIMULATOR_HPP
#define MANAGERLIFECYCLESIMULATOR_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 2
//

#include "ld-ev.hpp"

namespace module {

struct Conf {};

class ManagerLifecycleSimulator : public Everest::ModuleBase {
public:
    ManagerLifecycleSimulator() = delete;
    ManagerLifecycleSimulator(const ModuleInfo& info, Everest::MqttProvider& mqtt_provider, Conf& config) :
        ModuleBase(info), mqtt(mqtt_provider), config(config) {
    }

    Everest::MqttProvider& mqtt;
    const Conf& config;

private:
    friend class LdEverest;
    void init();
    void ready();
    void shutdown();

    bool blocked = false;
};

} // namespace module

#endif // MANAGERLIFECYCLESIMULATOR_HPP
