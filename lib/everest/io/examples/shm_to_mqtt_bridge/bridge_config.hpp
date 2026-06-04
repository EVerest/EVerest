// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace everest::lib::io::shm_to_mqtt_bridge {

enum class payload_mode {
    raw,
};

struct bridge_config {
    std::string name{"shm-inspect"};
};

struct shm_config {
    std::string client_id{"shm-mqtt-bridge"};
    std::string control_socket{"/tmp/everest-shm-control.sock"};
    bool control_socket_abstract_namespace{false};
    std::vector<std::string> topics;
    std::vector<std::string> topic_filters;
    bool subscribe_all{false};
};

struct mqtt_config {
    std::string host{"127.0.0.1"};
    std::uint16_t port{1883};
    std::string client_id{"shm-mqtt-bridge"};
    std::string topic_prefix{"everest/shm_inspect"};
    std::uint8_t qos{0};
    bool retain{false};
};

struct payload_config {
    payload_mode mode{payload_mode::raw};
};

struct app_config {
    bridge_config bridge;
    shm_config shm;
    mqtt_config mqtt;
    payload_config payload;
};

struct topic_resolution {
    std::vector<std::string> topics;
    std::vector<std::string> unmatched_filters;
    bool registry_queried{false};
};

/// \brief Parse a bridge YAML configuration from a file.
/// \throws std::runtime_error on missing/invalid configuration.
app_config parse_config(const std::string& filename);

/// \brief Parse a bridge YAML configuration from an in-memory string.
/// \throws std::runtime_error on missing/invalid configuration.
app_config parse_config_from_string(std::string_view yaml, std::string_view source);

/// \brief Strip leading and trailing slashes from a string.
std::string trim_slashes(std::string value);

/// \brief Compose the MQTT topic name for a given SHM topic using the configured prefix.
std::string mqtt_topic_for(const mqtt_config& config, std::string_view shm_topic);

/// \brief Expand the bridge's SHM subscription selectors against a registered topic list.
///
/// Behavior:
/// - Always includes every entry from \p config.topics.
/// - If \p config.subscribe_all is true, includes every entry from \p registered_topics.
/// - Expands each \p config.topic_filters entry against \p registered_topics; filters that match
///   no registered topic are returned in \c unmatched_filters and otherwise ignored.
/// - Returned topics are sorted and deduplicated.
topic_resolution resolve_subscription_topics(const shm_config& config,
                                             const std::vector<std::string>& registered_topics, bool registry_queried);

} // namespace everest::lib::io::shm_to_mqtt_bridge
