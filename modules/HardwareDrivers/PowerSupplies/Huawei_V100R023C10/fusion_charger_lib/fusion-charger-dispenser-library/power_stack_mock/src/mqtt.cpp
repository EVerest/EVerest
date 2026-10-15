// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "mqtt.hpp"

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {
constexpr std::uint32_t reconnect_timeout_ms = 1000;
constexpr std::uint16_t keepalive_seconds = 400;

using everest::lib::io::mqtt::ErrorCode;
using QoS = everest::lib::io::mqtt::mqtt_client::QoS;
} // namespace

MqttClient::MqttClient(const std::string& mqtt_host, const std::string& mqtt_port) : client(reconnect_timeout_ms) {
    client.set_error_handler([](int error, const std::string& message) {
        if (error != 0) {
            fprintf(stderr, "MQTT error: %s\n", message.c_str());
        }
    });

    const auto result = client.connect(mqtt_host, static_cast<std::uint16_t>(std::stoul(mqtt_port)), keepalive_seconds);
    if (result != ErrorCode::Success) {
        fprintf(stderr, "Failed to connect to MQTT broker: %s\n", std::string(to_string(result)).c_str());
        throw std::runtime_error("Failed to connect to MQTT broker");
    }

    event_thread = std::thread([this]() {
        event_handler.register_event_handler(&client);
        event_handler.register_event_handler(&stop_event, [this](const auto&) { running = false; });
        event_handler.run(running);
        client.disconnect();
    });
}

MqttClient::~MqttClient() {
    running = false;
    stop_event.notify();

    if (event_thread.joinable()) {
        event_thread.join();
    }
}

void MqttClient::publish(const std::string& topic, const std::string& message) {
    // the client is not thread-safe, so publishing is handed to the event thread
    event_handler.add_action([this, topic, message]() {
        const auto result = client.publish(topic, message, QoS::at_most_once, false, {});
        if (result != ErrorCode::Success) {
            fprintf(stderr, "Failed to publish message: %s\n", std::string(to_string(result)).c_str());
        }
    });
}
