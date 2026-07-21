// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "temperature_sensorImpl.hpp"

#include <chrono>
#include <thread>

namespace module {
namespace temperature_sensor {

namespace {

constexpr int MIN_PUBLISH_INTERVAL_MS = 20;

std::string cmd_topic(const std::string& module_id, const std::string& command) {
    return "everest_api/" + module_id + "/cmd/" + command;
}

} // namespace

types::temperature::Temperature temperature_sensorImpl::current_reading() const {
    types::temperature::Temperature reading;
    reading.temperature = temperature_C;
    reading.identification = identification;
    reading.location = location;
    return reading;
}

void temperature_sensorImpl::subscribe_mqtt_control_topics() {
    const auto module_id = mod->info.id;

    const std::string temperature_topic = cmd_topic(module_id, "set_temperature_C");
    EVLOG_info << "Control simulated temperature via MQTT topic: " << temperature_topic;
    mod->mqtt.subscribe(temperature_topic, [this](const std::string& payload) {
        try {
            const float value = std::stof(payload);
            {
                const std::lock_guard lock(state_mutex);
                temperature_C = value;
            }
            EVLOG_info << "Set simulated temperature to " << value << " C via MQTT";
        } catch (const std::invalid_argument&) {
            EVLOG_error << "Failed to set temperature via MQTT, invalid value: " << payload;
        }
    });

    const std::string identification_topic = cmd_topic(module_id, "set_identification");
    EVLOG_info << "Control simulated identification via MQTT topic: " << identification_topic;
    mod->mqtt.subscribe(identification_topic, [this](const std::string& payload) {
        {
            const std::lock_guard lock(state_mutex);
            identification = payload;
        }
        EVLOG_info << "Set simulated identification to \"" << payload << "\" via MQTT";
    });

    const std::string location_topic = cmd_topic(module_id, "set_location");
    EVLOG_info << "Control simulated location via MQTT topic: " << location_topic;
    mod->mqtt.subscribe(location_topic, [this](const std::string& payload) {
        {
            const std::lock_guard lock(state_mutex);
            location = payload;
        }
        EVLOG_info << "Set simulated location to \"" << payload << "\" via MQTT";
    });

    const std::string interval_topic = cmd_topic(module_id, "set_publish_interval_ms");
    EVLOG_info << "Control publish interval via MQTT topic: " << interval_topic;
    mod->mqtt.subscribe(interval_topic, [this](const std::string& payload) {
        try {
            const int interval_ms = std::stoi(payload);
            if (interval_ms < MIN_PUBLISH_INTERVAL_MS) {
                EVLOG_error << "Publish interval must be at least " << MIN_PUBLISH_INTERVAL_MS << " ms, got "
                            << interval_ms;
                return;
            }
            publish_interval_ms = interval_ms;
            EVLOG_info << "Set publish interval to " << interval_ms << " ms via MQTT";
        } catch (const std::invalid_argument&) {
            EVLOG_error << "Failed to set publish interval via MQTT, invalid value: " << payload;
        }
    });

    const std::string stop_topic = cmd_topic(module_id, "stop_publishing");
    EVLOG_info << "Stop publishing via MQTT topic: " << stop_topic;
    mod->mqtt.subscribe(stop_topic, [this](const std::string&) {
        publishing_active = false;
        EVLOG_info << "Stopped publishing simulated temperature readings";
    });

    const std::string start_topic = cmd_topic(module_id, "start_publishing");
    EVLOG_info << "Start publishing via MQTT topic: " << start_topic;
    mod->mqtt.subscribe(start_topic, [this](const std::string&) {
        publishing_active = true;
        EVLOG_info << "Started publishing simulated temperature readings";
    });
}

void temperature_sensorImpl::init() {
    {
        const std::lock_guard lock(state_mutex);
        temperature_C = static_cast<float>(config.temperature_C);
        identification = config.identification;
        location = config.location;
    }
    publish_interval_ms = config.publish_interval_ms;

    subscribe_mqtt_control_topics();
    publish_thread_handle = std::thread(&temperature_sensorImpl::publish_worker, this);
}

void temperature_sensorImpl::ready() {
}

void temperature_sensorImpl::publish_worker() {
    while (true) {
        if (publish_thread_handle.shouldExit()) {
            break;
        }

        if (publishing_active) {
            types::temperature::Temperature reading;
            {
                const std::lock_guard lock(state_mutex);
                reading = current_reading();
            }
            publish_temperatures({reading});
        }

        const int interval_ms = std::max(publish_interval_ms.load(), MIN_PUBLISH_INTERVAL_MS);
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    }
}

} // namespace temperature_sensor
} // namespace module
