// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/mqtt/mqtt_client.hpp>
#include <everest/io/shm/shm_client.hpp>

#include "bridge_config.hpp"

namespace {

using namespace std::chrono_literals;
using namespace everest::lib::io;
using everest::lib::io::shm_to_mqtt_bridge::app_config;
using everest::lib::io::shm_to_mqtt_bridge::mqtt_config;
using everest::lib::io::shm_to_mqtt_bridge::mqtt_topic_for;
using everest::lib::io::shm_to_mqtt_bridge::parse_config;
using everest::lib::io::shm_to_mqtt_bridge::resolve_subscription_topics;
using everest::lib::io::shm_to_mqtt_bridge::shm_config;
using everest::lib::io::shm_to_mqtt_bridge::trim_slashes;

constexpr auto mqtt_reconnect_timeout_ms = 2000U;
constexpr auto mqtt_keepalive_seconds = 30U;
constexpr auto mqtt_connect_timeout = 5s;
constexpr auto loop_poll_timeout = 500ms;

std::atomic_bool g_running{true};

void signal_handler(int signum) {
    (void)signum;
    g_running.store(false);
}

mqtt::mosquitto_cpp::QoS to_qos(std::uint8_t value) {
    switch (value) {
    case 0:
        return mqtt::mosquitto_cpp::QoS::at_most_once;
    case 1:
        return mqtt::mosquitto_cpp::QoS::at_least_once;
    case 2:
        return mqtt::mosquitto_cpp::QoS::exactly_once;
    }
    return mqtt::mosquitto_cpp::QoS::at_most_once;
}

void print_usage() {
    std::cout << "USAGE:\n";
    std::cout << "  shm_to_mqtt_bridge <config.yaml>\n\n";
    std::cout << "Republish SHM topics to MQTT under a configured prefix.\n";
    std::cout << "Topics can be selected via an explicit allowlist, MQTT-style filters, or "
                 "all framework-local SHM topics.\n";
}

void print_config(const app_config& config, const std::string& config_file) {
    std::cout << "SHM to MQTT inspection bridge\n";
    std::cout << "  config: " << config_file << "\n";
    std::cout << "  bridge.name: " << config.bridge.name << "\n";
    std::cout << "  shm.client_id: " << config.shm.client_id << "\n";
    std::cout << "  shm.control_socket: " << config.shm.control_socket << "\n";
    std::cout << "  shm.control_socket_abstract_namespace: "
              << (config.shm.control_socket_abstract_namespace ? "true" : "false") << "\n";
    std::cout << "  shm.subscribe_all: " << (config.shm.subscribe_all ? "true" : "false") << "\n";
    std::cout << "  shm.topics:";
    if (config.shm.topics.empty()) {
        std::cout << " (none)\n";
    } else {
        std::cout << "\n";
        for (const auto& topic : config.shm.topics) {
            std::cout << "    " << topic << " -> " << mqtt_topic_for(config.mqtt, topic) << "\n";
        }
    }
    std::cout << "  shm.topic_filters:";
    if (config.shm.topic_filters.empty()) {
        std::cout << " (none)\n";
    } else {
        std::cout << "\n";
        for (const auto& filter : config.shm.topic_filters) {
            std::cout << "    " << filter << "\n";
        }
    }
    std::cout << "  mqtt.host: " << config.mqtt.host << "\n";
    std::cout << "  mqtt.port: " << config.mqtt.port << "\n";
    std::cout << "  mqtt.client_id: " << config.mqtt.client_id << "\n";
    std::cout << "  mqtt.topic_prefix: " << trim_slashes(config.mqtt.topic_prefix) << "\n";
    std::cout << "  mqtt.qos: " << static_cast<int>(config.mqtt.qos) << "\n";
    std::cout << "  mqtt.retain: " << (config.mqtt.retain ? "true" : "false") << "\n";
    std::cout << "  payload.mode: raw\n";
    std::cout << std::flush;
}

bool io_ok(const shm::io_result& result, const std::string& context) {
    if (result) {
        return true;
    }
    std::cerr << "ERROR: " << context << ": " << shm::to_string(result.status);
    if (!result.message.empty()) {
        std::cerr << ": " << result.message;
    }
    std::cerr << "\n";
    return false;
}

bool connect_mqtt(event::fd_event_handler& handler, mqtt::mqtt_client& client, const mqtt_config& config) {
    bool callback_seen = false;
    bool connected = false;
    std::string connect_error;

    client.set_callback_connect([&](auto&, auto rc, auto, auto const&) {
        callback_seen = true;
        if (rc == mqtt::mosquitto_cpp::ResponseCode::Success) {
            connected = true;
            std::cout << "Connected to MQTT broker " << config.host << ":" << config.port << "\n";
        } else {
            connect_error = "broker rejected connection with response code " + std::to_string(static_cast<int>(rc));
        }
    });
    client.set_callback_disconnect([&](auto&, auto ec, auto const&) {
        if (ec != mqtt::ErrorCode::Success) {
            std::cerr << "ERROR: MQTT disconnected: " << mqtt::to_string(ec) << "\n";
        }
    });
    client.set_error_handler([](int error, const std::string& message) {
        if (error != 0) {
            std::cerr << "ERROR: MQTT client: " << error << ": " << message << "\n";
        }
    });

    const auto connect_result = client.connect(config.host, config.port, mqtt_keepalive_seconds);
    if (connect_result != mqtt::ErrorCode::Success) {
        std::cerr << "ERROR: MQTT connection failure: " << mqtt::to_string(connect_result) << "\n";
        return false;
    }
    if (!handler.register_event_handler(&client)) {
        std::cerr << "ERROR: failed to register MQTT event handler\n";
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() + mqtt_connect_timeout;
    while (g_running.load() && !callback_seen && std::chrono::steady_clock::now() < deadline) {
        handler.poll(100ms);
        handler.run_actions();
    }

    if (!connected) {
        std::cerr << "ERROR: MQTT connection failure";
        if (!connect_error.empty()) {
            std::cerr << ": " << connect_error;
        } else {
            std::cerr << ": timed out waiting for CONNACK";
        }
        std::cerr << "\n";
        return false;
    }

    return true;
}

shm::client_options make_shm_options(const shm_config& config) {
    shm::client_options options;
    options.client_id = config.client_id;
    options.control.server_name = config.control_socket;
    options.control.server_abstract_namespace = config.control_socket_abstract_namespace;
    return options;
}

int run_bridge(const std::string& config_file) {
    const auto config = parse_config(config_file);
    print_config(config, config_file);

    event::fd_event_handler handler;
    mqtt::mqtt_client mqtt_client(mqtt_reconnect_timeout_ms, config.mqtt.client_id);
    if (!connect_mqtt(handler, mqtt_client, config.mqtt)) {
        return EXIT_FAILURE;
    }

    shm::shm_client shm_client(make_shm_options(config.shm));
    shm_client.set_error_handler([](shm::io_status status, std::string_view message) {
        std::cerr << "ERROR: SHM client: " << shm::to_string(status);
        if (!message.empty()) {
            std::cerr << ": " << message;
        }
        std::cerr << "\n";
    });

    if (!io_ok(shm_client.connect(), "SHM control socket connection/handshake failure")) {
        handler.unregister_event_handler(&mqtt_client);
        mqtt_client.disconnect();
        return EXIT_FAILURE;
    }

    const bool needs_registry = config.shm.subscribe_all || !config.shm.topic_filters.empty();
    std::vector<std::string> registered_topics;
    if (needs_registry) {
        auto registry = shm_client.registered_topics();
        if (!registry) {
            io_ok(shm::io_result{registry.status, registry.message}, "SHM registry discovery failure");
            shm_client.disconnect();
            handler.unregister_event_handler(&mqtt_client);
            mqtt_client.disconnect();
            return EXIT_FAILURE;
        }
        registered_topics = std::move(registry.topics);
    }

    auto resolved = resolve_subscription_topics(config.shm, registered_topics, needs_registry);
    for (const auto& filter : resolved.unmatched_filters) {
        std::cerr << "WARNING: SHM topic filter '" << filter
                  << "' did not match any registered topic; ignoring this filter\n";
    }
    if (resolved.topics.empty()) {
        if (needs_registry && registered_topics.empty()) {
            std::cerr << "ERROR: SHM registry is empty; no topics to subscribe to\n";
        } else {
            std::cerr << "ERROR: SHM subscription set resolved to zero topics\n";
        }
        shm_client.disconnect();
        handler.unregister_event_handler(&mqtt_client);
        mqtt_client.disconnect();
        return EXIT_FAILURE;
    }

    std::cout << "Resolved SHM subscription topics (" << resolved.topics.size() << "):\n";
    for (const auto& topic : resolved.topics) {
        std::cout << "  " << topic << " -> " << mqtt_topic_for(config.mqtt, topic) << "\n";
    }

    std::uint64_t forwarded_count = 0;
    std::uint64_t publish_failure_count = 0;
    const auto qos = to_qos(config.mqtt.qos);

    const auto callback = [&](std::string_view shm_topic, std::string_view payload) {
        const auto mqtt_topic = mqtt_topic_for(config.mqtt, shm_topic);
        const auto result = mqtt_client.publish(mqtt_topic, payload, qos, config.mqtt.retain, {});
        if (result == mqtt::ErrorCode::Success) {
            ++forwarded_count;
        } else {
            ++publish_failure_count;
            std::cerr << "ERROR: MQTT publish failure for SHM topic '" << shm_topic << "' to MQTT topic '" << mqtt_topic
                      << "': " << mqtt::to_string(result) << "\n";
        }
    };

    shm::subscribe_options subscribe_options;
    const auto subscribe_result = shm_client.subscribe(resolved.topics, callback, subscribe_options);
    if (!io_ok(subscribe_result, "SHM subscribe failure")) {
        if (subscribe_result.status == shm::io_status::unknown_topic) {
            std::cerr << "ERROR: one or more resolved SHM topics are unknown to the SHM manager\n";
        }
        shm_client.disconnect();
        handler.unregister_event_handler(&mqtt_client);
        mqtt_client.disconnect();
        return EXIT_FAILURE;
    }

    if (!handler.register_event_handler(static_cast<event::fd_event_register_interface*>(&shm_client))) {
        std::cerr << "ERROR: failed to register SHM event handler\n";
        shm_client.disconnect();
        handler.unregister_event_handler(&mqtt_client);
        mqtt_client.disconnect();
        return EXIT_FAILURE;
    }

    std::cout << "Bridge running; press Ctrl+C to stop.\n";
    while (g_running.load() && shm_client.is_connected()) {
        handler.poll(loop_poll_timeout);
        handler.run_actions();
    }
    const bool shm_server_lost = !shm_client.is_connected() && g_running.load();
    if (shm_server_lost) {
        std::cerr << "ERROR: SHM server disconnected; stopping bridge.\n";
    }

    std::cout << "Stopping bridge. Forwarded " << forwarded_count << " message(s), MQTT publish failures "
              << publish_failure_count << ".\n";

    bool cleanup_ok = true;
    if (shm_client.is_connected()) {
        for (const auto& topic : resolved.topics) {
            cleanup_ok = io_ok(shm_client.unsubscribe(topic), "SHM unsubscribe '" + topic + "'") && cleanup_ok;
        }
    }
    cleanup_ok =
        handler.unregister_event_handler(static_cast<event::fd_event_register_interface*>(&shm_client)) && cleanup_ok;
    cleanup_ok = io_ok(shm_client.disconnect(), "SHM disconnect") && cleanup_ok;
    cleanup_ok = handler.unregister_event_handler(&mqtt_client) && cleanup_ok;
    const auto disconnect_result = mqtt_client.disconnect();
    if (disconnect_result != mqtt::ErrorCode::Success) {
        std::cerr << "ERROR: MQTT disconnect failure: " << mqtt::to_string(disconnect_result) << "\n";
        cleanup_ok = false;
    }

    return cleanup_ok && !shm_server_lost ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        print_usage();
        return EXIT_SUCCESS;
    }
    if (argc != 2) {
        print_usage();
        return EXIT_FAILURE;
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        return run_bridge(argv[1]);
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
