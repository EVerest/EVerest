// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <atomic>
#include <string>
#include <thread>

#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/mqtt/mqtt_client.hpp>

class MqttClient {
public:
    MqttClient(const std::string& mqtt_host, const std::string& mqtt_port);
    ~MqttClient();

    MqttClient(const MqttClient&) = delete;
    MqttClient& operator=(const MqttClient&) = delete;
    MqttClient(MqttClient&&) = delete;
    MqttClient& operator=(MqttClient&&) = delete;

    void publish(const std::string& topic, const std::string& message);

private:
    everest::lib::io::mqtt::mqtt_client client;
    everest::lib::io::event::fd_event_handler event_handler;
    everest::lib::io::event::event_fd stop_event;
    std::atomic_bool running{true};
    std::thread event_thread;
};
