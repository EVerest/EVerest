// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "bridge_config.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <everest/io/shm/topic_filter.hpp>

// clang-format off
#include <ryml_std.hpp>
#include <ryml.hpp>
// clang-format on

namespace everest::lib::io::shm_to_mqtt_bridge {

namespace {

[[noreturn]] void config_error(const std::string& message) {
    throw std::runtime_error("Configuration error: " + message);
}

std::string scalar_to_string(c4::yml::ConstNodeRef node) {
    if (!node.has_val()) {
        config_error("expected scalar value");
    }
    const auto value = node.val();
    return std::string(value.str, value.len);
}

template <typename T> T read_scalar(c4::yml::ConstNodeRef node, const std::string& path) {
    if (node.invalid()) {
        config_error("missing required key '" + path + "'");
    }
    try {
        T value{};
        node >> value;
        return value;
    } catch (const std::exception& e) {
        config_error("invalid value for '" + path + "': " + e.what());
    }
}

template <typename T> T read_scalar_or(c4::yml::ConstNodeRef node, const std::string& path, T fallback) {
    if (node.invalid()) {
        return fallback;
    }
    return read_scalar<T>(node, path);
}

c4::yml::ConstNodeRef optional_child(c4::yml::ConstNodeRef node, const char* key) {
    if (node.invalid()) {
        return {};
    }
    return node.find_child(ryml::to_csubstr(key));
}

c4::yml::ConstNodeRef required_block(c4::yml::ConstNodeRef root, const char* key) {
    auto block = optional_child(root, key);
    if (block.invalid()) {
        config_error(std::string("missing required block '") + key + "'");
    }
    if (!block.is_map()) {
        config_error(std::string("block '") + key + "' must be a map");
    }
    return block;
}

std::vector<std::string> read_topic_sequence(c4::yml::ConstNodeRef block, const char* key,
                                             const char* missing_entry_msg, const char* duplicate_msg,
                                             bool wildcard_allowed) {
    const auto sequence = optional_child(block, key);
    if (sequence.invalid()) {
        return {};
    }
    if (!sequence.is_seq()) {
        config_error(std::string("'") + key + "' must be a sequence");
    }

    std::vector<std::string> result;
    std::set<std::string> seen;
    for (const auto child : sequence.children()) {
        auto value = scalar_to_string(child);
        if (value.empty()) {
            config_error(missing_entry_msg);
        }
        if (!seen.insert(value).second) {
            config_error(std::string(duplicate_msg) + " '" + value + "'");
        }
        if (wildcard_allowed) {
            if (!everest::lib::io::shm::is_valid_topic_filter(value)) {
                config_error("invalid MQTT-style topic filter '" + value + "'");
            }
        } else {
            if (value.find('+') != std::string::npos || value.find('#') != std::string::npos) {
                config_error("'" + value + "' is not an exact SHM topic; use 'shm.topic_filters' for wildcards");
            }
        }
        result.push_back(std::move(value));
    }
    return result;
}

payload_mode read_payload_mode(c4::yml::ConstNodeRef payload_block) {
    const auto value = read_scalar_or<std::string>(optional_child(payload_block, "mode"), "payload.mode", "raw");
    if (value == "raw") {
        return payload_mode::raw;
    }
    config_error("unsupported payload.mode '" + value + "'; supported value is 'raw'");
}

app_config parse_app_config(ryml::Tree tree) {
    const auto root = tree.rootref();
    if (root.invalid() || !root.is_map()) {
        config_error("top-level YAML document must be a map");
    }

    app_config config;

    const auto bridge_block = required_block(root, "bridge");
    config.bridge.name =
        read_scalar_or<std::string>(optional_child(bridge_block, "name"), "bridge.name", config.bridge.name);
    if (config.bridge.name.empty()) {
        config_error("'bridge.name' must not be empty");
    }

    const auto shm_block = required_block(root, "shm");
    config.shm.client_id =
        read_scalar_or<std::string>(optional_child(shm_block, "client_id"), "shm.client_id", config.shm.client_id);
    config.shm.control_socket = read_scalar_or<std::string>(optional_child(shm_block, "control_socket"),
                                                            "shm.control_socket", config.shm.control_socket);
    config.shm.control_socket_abstract_namespace = read_scalar_or<bool>(
        optional_child(shm_block, "control_socket_abstract_namespace"), "shm.control_socket_abstract_namespace", false);
    config.shm.topics = read_topic_sequence(shm_block, "topics", "'shm.topics' entries must not be empty",
                                            "duplicate SHM topic", false);
    config.shm.topic_filters =
        read_topic_sequence(shm_block, "topic_filters", "'shm.topic_filters' entries must not be empty",
                            "duplicate SHM topic filter", true);
    config.shm.subscribe_all =
        read_scalar_or<bool>(optional_child(shm_block, "subscribe_all"), "shm.subscribe_all", false);
    if (config.shm.topics.empty() && config.shm.topic_filters.empty() && !config.shm.subscribe_all) {
        config_error("'shm' must specify at least one of 'topics', 'topic_filters', or 'subscribe_all'");
    }
    if (config.shm.client_id.empty()) {
        config_error("'shm.client_id' must not be empty");
    }
    if (config.shm.control_socket.empty()) {
        config_error("'shm.control_socket' must not be empty");
    }

    const auto mqtt_block = required_block(root, "mqtt");
    config.mqtt.host = read_scalar_or<std::string>(optional_child(mqtt_block, "host"), "mqtt.host", config.mqtt.host);
    config.mqtt.port = read_scalar_or<std::uint16_t>(optional_child(mqtt_block, "port"), "mqtt.port", config.mqtt.port);
    config.mqtt.client_id =
        read_scalar_or<std::string>(optional_child(mqtt_block, "client_id"), "mqtt.client_id", config.mqtt.client_id);
    config.mqtt.topic_prefix = read_scalar_or<std::string>(optional_child(mqtt_block, "topic_prefix"),
                                                           "mqtt.topic_prefix", config.mqtt.topic_prefix);
    const auto qos = read_scalar_or<int>(optional_child(mqtt_block, "qos"), "mqtt.qos", 0);
    config.mqtt.retain = read_scalar_or<bool>(optional_child(mqtt_block, "retain"), "mqtt.retain", false);
    if (config.mqtt.host.empty()) {
        config_error("'mqtt.host' must not be empty");
    }
    if (config.mqtt.client_id.empty()) {
        config_error("'mqtt.client_id' must not be empty");
    }
    if (config.mqtt.topic_prefix.empty()) {
        config_error("'mqtt.topic_prefix' must not be empty");
    }
    if (qos < 0 || qos > 2) {
        config_error("'mqtt.qos' must be 0, 1, or 2");
    }
    config.mqtt.qos = static_cast<std::uint8_t>(qos);

    const auto payload_block = optional_child(root, "payload");
    if (!payload_block.invalid()) {
        if (!payload_block.is_map()) {
            config_error("block 'payload' must be a map");
        }
        config.payload.mode = read_payload_mode(payload_block);
    }

    return config;
}

ryml::Tree parse_tree(std::string_view yaml, std::string_view source) {
    ryml::EventHandlerTree event_handler = {};
    ryml::Parser parser(&event_handler, ryml::ParserOptions().locations(true));
    ryml::Tree tree;
    ryml::parse_in_arena(&parser, ryml::csubstr(source.data(), source.size()), ryml::csubstr(yaml.data(), yaml.size()),
                         &tree);
    return tree;
}

} // namespace

app_config parse_config_from_string(std::string_view yaml, std::string_view source) {
    return parse_app_config(parse_tree(yaml, source));
}

app_config parse_config(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open config file: " + filename);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string content = buffer.str();
    return parse_config_from_string(content, filename);
}

std::string trim_slashes(std::string value) {
    const auto first = value.find_first_not_of('/');
    if (first == std::string::npos) {
        value.clear();
        return value;
    }
    value.erase(0, first);
    const auto last = value.find_last_not_of('/');
    if (last != std::string::npos) {
        value.erase(last + 1);
    }
    return value;
}

std::string mqtt_topic_for(const mqtt_config& config, std::string_view shm_topic) {
    auto prefix = trim_slashes(config.topic_prefix);
    auto topic = trim_slashes(std::string(shm_topic));
    return prefix + "/" + topic;
}

topic_resolution resolve_subscription_topics(const shm_config& config,
                                             const std::vector<std::string>& registered_topics, bool registry_queried) {
    topic_resolution result;
    result.registry_queried = registry_queried;
    std::set<std::string> selected;

    for (const auto& topic : config.topics) {
        if (selected.insert(topic).second) {
            result.topics.push_back(topic);
        }
    }

    if (config.subscribe_all) {
        for (const auto& topic : registered_topics) {
            if (selected.insert(topic).second) {
                result.topics.push_back(topic);
            }
        }
    }

    for (const auto& filter : config.topic_filters) {
        bool matched_any = false;
        for (const auto& topic : registered_topics) {
            if (everest::lib::io::shm::topic_filter_matches(filter, topic)) {
                matched_any = true;
                if (selected.insert(topic).second) {
                    result.topics.push_back(topic);
                }
            }
        }
        if (!matched_any) {
            result.unmatched_filters.push_back(filter);
        }
    }

    std::sort(result.topics.begin(), result.topics.end());
    return result;
}

} // namespace everest::lib::io::shm_to_mqtt_bridge
