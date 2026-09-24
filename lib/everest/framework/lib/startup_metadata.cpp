// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <utils/startup_metadata.hpp>

#include <string>
#include <vector>

#include <fmt/core.h>

#include <utils/types.hpp>

namespace Everest {

void publish_startup_metadata(const ManagerConfig& config, MQTTAbstraction& mqtt, const ManagerSettings& ms) {
    const auto& everest_prefix = ms.mqtt_settings.everest_prefix;

    const auto interface_definitions = config.get_interface_definitions();
    std::vector<std::string> interface_names;
    for (const auto& interface_definition : interface_definitions.items()) {
        interface_names.push_back(interface_definition.key());
    }

    const MqttMessagePayload payload{MqttMessageType::ConfigurationResponse, interface_names};
    mqtt.publish(fmt::format("{}interfaces", everest_prefix), payload, QOS::QOS2, true);

    for (const auto& interface_definition : interface_definitions.items()) {
        const MqttMessagePayload interface_definition_payload{MqttMessageType::ConfigurationResponse,
                                                              interface_definition.value()};
        mqtt.publish(fmt::format("{}interface_definitions/{}", everest_prefix, interface_definition.key()),
                     interface_definition_payload, QOS::QOS2, true);
    }

    const auto type_definitions = config.get_types();
    std::vector<std::string> type_names;
    for (const auto& type_definition : type_definitions.items()) {
        type_names.push_back(type_definition.key());
    }

    const MqttMessagePayload type_names_payload{MqttMessageType::ConfigurationResponse, type_names};
    mqtt.publish(fmt::format("{}types", everest_prefix), type_names_payload, QOS::QOS2, true);

    for (const auto& type_definition : type_definitions.items()) {
        const MqttMessagePayload type_definition_payload{MqttMessageType::ConfigurationResponse,
                                                         type_definition.value()};
        // type_definition keys already start with a / so omit it in the topic name
        mqtt.publish(fmt::format("{}type_definitions{}", everest_prefix, type_definition.key()),
                     type_definition_payload, QOS::QOS2, true);
    }

    const MqttMessagePayload settings_payload{MqttMessageType::ConfigurationResponse, config.get_settings()};
    mqtt.publish(fmt::format("{}settings", everest_prefix), settings_payload, QOS::QOS2, true);

    if (ms.runtime_settings.validate_schema) {
        const MqttMessagePayload schemas_payload{MqttMessageType::ConfigurationResponse, config.get_schemas()};
        mqtt.publish(fmt::format("{}schemas", everest_prefix), schemas_payload, QOS::QOS2, true);
    }

    const auto manifests = config.get_manifests();
    for (const auto& manifest : manifests.items()) {
        auto manifest_copy = manifest.value();
        manifest_copy.erase("config");

        const MqttMessagePayload manifest_payload{MqttMessageType::ConfigurationResponse, manifest_copy};
        mqtt.publish(fmt::format("{}manifests/{}", everest_prefix, manifest.key()), manifest_payload, QOS::QOS2, true);
    }

    const MqttMessagePayload module_names_payload{MqttMessageType::ConfigurationResponse, config.get_module_names()};
    mqtt.publish(fmt::format("{}module_names", everest_prefix), module_names_payload, QOS::QOS2, true);
}

} // namespace Everest
