// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef TEMPERATURE_SENSOR_TEMPERATURE_SENSOR_IMPL_HPP
#define TEMPERATURE_SENSOR_TEMPERATURE_SENSOR_IMPL_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 3
//

#include <generated/interfaces/temperature_sensor/Implementation.hpp>

#include "../TemperatureSensorSimulator.hpp"

// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1
// insert your custom include headers here
#include <atomic>
#include <mutex>
#include <string>
#include <utils/thread.hpp>
// ev@75ac1216-19eb-4182-a85c-820f1fc2c091:v1

namespace module {
namespace temperature_sensor {

struct Conf {
    double temperature_C;
    std::string identification;
    std::string location;
    int publish_interval_ms;
};

class temperature_sensorImpl : public temperature_sensorImplBase {
public:
    temperature_sensorImpl() = delete;
    temperature_sensorImpl(Everest::ModuleAdapter* ev, const Everest::PtrContainer<TemperatureSensorSimulator>& mod,
                           Conf& config) :
        temperature_sensorImplBase(ev, "temperature_sensor"), mod(mod), config(config){};

    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1
    // insert your public definitions here
    // ev@8ea32d28-373f-4c90-ae5e-b4fcc74e2a61:v1

protected:
    // no commands defined for this interface

    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1
    // insert your protected definitions here
    // ev@d2d1847a-7b88-41dd-ad07-92785f06f5c4:v1

private:
    const Everest::PtrContainer<TemperatureSensorSimulator>& mod;
    const Conf& config;

    virtual void init() override;
    virtual void ready() override;

    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
    // insert your private definitions here
    std::mutex state_mutex;
    float temperature_C{25.0f};
    std::string identification;
    std::string location;
    std::atomic<int> publish_interval_ms{1000};
    std::atomic<bool> publishing_active{true};

    Everest::Thread publish_thread_handle;
    void publish_worker();
    void subscribe_mqtt_control_topics();
    types::temperature::Temperature current_reading() const;
    // ev@3370e4dd-95f4-47a9-aaec-ea76f34a66c9:v1
};

// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1
// insert other definitions here
// ev@3d7da0ad-02c2-493d-9920-0bbbd56b9876:v1

} // namespace temperature_sensor
} // namespace module

#endif // TEMPERATURE_SENSOR_TEMPERATURE_SENSOR_IMPL_HPP
