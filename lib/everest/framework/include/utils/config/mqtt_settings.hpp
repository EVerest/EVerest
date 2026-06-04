// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef UTILS_CONFIG_MQTT_SETTINGS_HPP
#define UTILS_CONFIG_MQTT_SETTINGS_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace Everest {

enum class FrameworkTransportType {
    MQTT,
    SHM,
};

enum class ShmTopicRegistryMode {
    Static,
    Dynamic,
};

/// \brief minimal MQTT connection settings needed for an initial connection of a module to the manager
struct MQTTSettings {
    std::string broker_socket_path; ///< A path to a socket the MQTT broker uses in socket mode. If this is set
                                    ///< broker_host and broker_port are ignored
    std::string broker_host;        ///< The hostname of the MQTT broker.
    std::uint16_t broker_port = 0;  ///< The port the MQTT broker listens on
    FrameworkTransportType framework_transport = FrameworkTransportType::MQTT; ///< Framework-local transport.
    std::string shm_control_socket_path;           ///< Framework-local SHM control socket endpoint.
    std::uint32_t shm_topic_slots = 0;             ///< Default SHM ring slots per topic.
    std::uint32_t shm_topic_slot_size = 0;         ///< Default SHM slot size per topic.
    std::uint32_t shm_topic_registry_capacity = 0; ///< Minimum SHM topic registry capacity.
    ShmTopicRegistryMode shm_topic_registry_mode =
        ShmTopicRegistryMode::Static; ///< SHM topic registry mode. Only static Manager-precomputed mode is supported.
    std::vector<std::string> shm_registered_topics; ///< Framework-local exact SHM topics registered by the Manager.
    std::string everest_prefix;                     ///< MQTT topic prefix for the "everest" topic
    std::string external_prefix;                    ///< MQTT topic prefix for external topics

    /// \brief Indicates if a Unix Domain Socket is used for connection to the MQTT broker
    /// \returns true is a UDS is used, false if a connection via host and port is used
    bool uses_socket() const;
    bool shared_mem() const;
};

std::string framework_transport_to_string(FrameworkTransportType framework_transport);
FrameworkTransportType framework_transport_from_string(const std::string& framework_transport);
std::string shm_topic_registry_mode_to_string(ShmTopicRegistryMode mode);
ShmTopicRegistryMode shm_topic_registry_mode_from_string(const std::string& mode);

/// \brief Parses a comma-separated list of SHM framework topics (as produced by the Manager when populating
/// EV_SHM_REGISTERED_TOPICS) into a vector. Empty entries are skipped.
std::vector<std::string> parse_shm_registered_topics(const std::string& comma_separated);

/// \brief Creates MQTTSettings with a Unix Domain Socket with the provided \p mqtt_broker_socket_path
/// using the \p mqtt_everest_prefix and \p mqtt_external_prefix
MQTTSettings create_mqtt_settings(const std::string& mqtt_broker_socket_path, const std::string& mqtt_everest_prefix,
                                  const std::string& mqtt_external_prefix);

/// \brief Creates MQTTSettings for IP based connections with the provided \p mqtt_broker_host
/// and \p mqtt_broker_port using the \p mqtt_everest_prefix and \p mqtt_external_prefix
MQTTSettings create_mqtt_settings(const std::string& mqtt_broker_host, std::uint16_t mqtt_broker_port,
                                  const std::string& mqtt_everest_prefix, const std::string& mqtt_external_prefix);

/// \brief Populates the given MQTTSettings \p mqtt_settings with a Unix Domain Socket with the provided \p
/// mqtt_broker_socket_path using the \p mqtt_everest_prefix and \p mqtt_external_prefix
void populate_mqtt_settings(MQTTSettings& mqtt_settings, const std::string& mqtt_broker_socket_path,
                            const std::string& mqtt_everest_prefix, const std::string& mqtt_external_prefix);

/// \brief  Populates the given MQTTSettings \p mqtt_settings for IP based connections with the provided \p
/// mqtt_broker_host and \p mqtt_broker_port using the \p mqtt_everest_prefix and \p mqtt_external_prefix
void populate_mqtt_settings(MQTTSettings& mqtt_settings, const std::string& mqtt_broker_host,
                            std::uint16_t mqtt_broker_port, const std::string& mqtt_everest_prefix,
                            const std::string& mqtt_external_prefix);

} // namespace Everest

#endif // UTILS_CONFIG_MQTT_SETTINGS_HPP
