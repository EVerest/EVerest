// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <utils/config/mqtt_settings.hpp>

#include <sstream>
#include <stdexcept>

namespace Everest {

bool MQTTSettings::uses_socket() const {
    if (not broker_socket_path.empty()) {
        return true;
    }
    return false;
}

bool MQTTSettings::shared_mem() const {
    return framework_transport == FrameworkTransportType::SHM;
}

std::string framework_transport_to_string(FrameworkTransportType framework_transport) {
    switch (framework_transport) {
    case FrameworkTransportType::MQTT:
        return "mqtt";
    case FrameworkTransportType::SHM:
        return "shm";
    }
    throw std::logic_error("Unknown framework transport type");
}

FrameworkTransportType framework_transport_from_string(const std::string& framework_transport) {
    if (framework_transport == "mqtt") {
        return FrameworkTransportType::MQTT;
    }
    if (framework_transport == "shm") {
        return FrameworkTransportType::SHM;
    }
    throw std::invalid_argument("framework_transport must be either 'mqtt' or 'shm'");
}

std::string shm_topic_registry_mode_to_string(ShmTopicRegistryMode mode) {
    switch (mode) {
    case ShmTopicRegistryMode::Static:
        return "static";
    case ShmTopicRegistryMode::Dynamic:
        return "dynamic";
    }
    throw std::logic_error("Unknown SHM topic registry mode");
}

ShmTopicRegistryMode shm_topic_registry_mode_from_string(const std::string& mode) {
    if (mode == "static" || mode == "precomputed" || mode == "static_precomputed") {
        return ShmTopicRegistryMode::Static;
    }
    if (mode == "dynamic") {
        return ShmTopicRegistryMode::Dynamic;
    }
    throw std::invalid_argument(
        "shm_topic_registry_mode must be 'static' (aliases: 'precomputed', 'static_precomputed') or 'dynamic'");
}

std::vector<std::string> parse_shm_registered_topics(const std::string& comma_separated) {
    std::vector<std::string> topics;
    if (comma_separated.empty()) {
        return topics;
    }
    std::istringstream stream(comma_separated);
    std::string topic;
    while (std::getline(stream, topic, ',')) {
        if (!topic.empty()) {
            topics.push_back(topic);
        }
    }
    return topics;
}

MQTTSettings create_mqtt_settings(const std::string& mqtt_broker_socket_path, const std::string& mqtt_everest_prefix,
                                  const std::string& mqtt_external_prefix) {
    MQTTSettings mqtt_settings;
    populate_mqtt_settings(mqtt_settings, mqtt_broker_socket_path, mqtt_everest_prefix, mqtt_external_prefix);
    return mqtt_settings;
}

MQTTSettings create_mqtt_settings(const std::string& mqtt_broker_host, std::uint16_t mqtt_broker_port,
                                  const std::string& mqtt_everest_prefix, const std::string& mqtt_external_prefix) {
    MQTTSettings mqtt_settings;
    populate_mqtt_settings(mqtt_settings, mqtt_broker_host, mqtt_broker_port, mqtt_everest_prefix,
                           mqtt_external_prefix);
    return mqtt_settings;
}

void populate_mqtt_settings(MQTTSettings& mqtt_settings, const std::string& mqtt_broker_socket_path,
                            const std::string& mqtt_everest_prefix, const std::string& mqtt_external_prefix) {
    mqtt_settings.broker_socket_path = mqtt_broker_socket_path;
    mqtt_settings.everest_prefix = mqtt_everest_prefix;
    mqtt_settings.external_prefix = mqtt_external_prefix;
}

void populate_mqtt_settings(MQTTSettings& mqtt_settings, const std::string& mqtt_broker_host,
                            std::uint16_t mqtt_broker_port, const std::string& mqtt_everest_prefix,
                            const std::string& mqtt_external_prefix) {
    mqtt_settings.broker_host = mqtt_broker_host;
    mqtt_settings.broker_port = mqtt_broker_port;
    mqtt_settings.everest_prefix = mqtt_everest_prefix;
    mqtt_settings.external_prefix = mqtt_external_prefix;
}

} // namespace Everest
