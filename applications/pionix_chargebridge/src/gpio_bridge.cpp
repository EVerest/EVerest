// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include <charge_bridge/gpio_bridge.hpp>
#include <charge_bridge/utilities/logging.hpp>
#include <charge_bridge/utilities/platform_utils.hpp>
#include <charge_bridge/utilities/string.hpp>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/mqtt/mqtt_client.hpp>
#include <iostream>
#include <limits>
#include <memory>
#include <protocol/cb_management.h>
#include <stdexcept>
#include <string>

namespace charge_bridge {
using namespace std::chrono_literals;
namespace mqtt = everest::lib::io::mqtt;

namespace {
const int default_udp_timeout_ms = 1000;
const int mqtt_reconnect_timeout_ms = 1000;
} // namespace

gpio_bridge::gpio_bridge(gpio_config const& config, everest::lib::io::event::event_fd& ready_notify) :
    m_mqtt(mqtt_reconnect_timeout_ms),
    m_udp_port(config.cb_port),
    m_udp_remote(config.cb_remote),
    m_ready_notify(ready_notify)

{
    m_identifier = config.cb + "/" + config.item;

    m_heartbeat_timer.set_timeout(std::chrono::seconds(config.interval_s));

    create_udp_client(m_udp_remote, m_udp_port);

    m_receive_topic = "pionix/chargebridge/" + config.cb + "/gpio/output/";
    m_send_topic = "pionix/chargebridge/" + config.cb + "/gpio/input/";

    m_mqtt.set_error_handler([this, config](int id, std::string const& msg) {
        utilities::print_error(m_identifier, "GPIO/MQTT", id) << msg << std::endl;
        m_mqtt_on_error = id not_eq 0;
        m_mqtt_ready = id == 0;
        handle_ready();
    });

    m_mqtt.set_callback_connect([this](auto&, auto, auto, auto const&) {
        m_mqtt.subscribe(
            m_receive_topic + "#", [this](auto&, auto const& payload) { dispatch(payload); },
            everest::lib::io::mqtt::mqtt_client::QoS::at_most_once);
    });

    m_mqtt.connect(config.mqtt_bind, config.mqtt_remote, config.mqtt_port, config.mqtt_ping_interval_ms);

    m_message.type = CbStructType::CST_HostToCb_Gpio;
    m_message.data.number_of_gpios = CB_NUMBER_OF_GPIOS;
    std::memset(m_message.data.gpio_values, 0, sizeof(m_message.data.gpio_values));

    m_ready.setCallback([this](auto&, auto&) { m_ready_notify.notify(); });
    m_cb_is_connected.setCallback([this](bool last, bool current) {
        if (not last and current) {
            if (m_udp) {
                m_udp->reset();
            }
        }
        handle_ready();
    });
}

void gpio_bridge::create_udp_client(std::string const& remote, uint16_t remote_port) {
    m_udp = std::make_unique<everest::lib::io::udp::udp_client>(remote, remote_port, default_udp_timeout_ms);
    m_udp_ready = false;
    m_udp_on_error = false;
    m_udp->set_rx_handler([this](auto const& data, auto&) { handle_udp_rx(data); });
    m_udp->set_error_handler([this](auto id, auto const& msg) {
        utilities::print_error(m_identifier, "GPIO/UDP", id) << msg << std::endl;
        m_udp_on_error = id not_eq 0;
        m_udp_ready = id == 0;
        handle_ready();
    });
}

void gpio_bridge::disconnect_cb_endpoint() {
    m_udp_ready = false;
    m_udp_on_error = true;
    if (m_udp) {
        m_udp->reset();
    }
    m_udp.reset();
    handle_ready();
}

void gpio_bridge::connect_cb_endpoint(std::string const& remote) {
    m_udp_remote = remote;
    disconnect_cb_endpoint();
    create_udp_client(m_udp_remote, m_udp_port);
    handle_ready();
}

gpio_bridge::~gpio_bridge() {
}

bool gpio_bridge::register_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = handler.register_event_handler(m_udp.get());
    result = handler.register_event_handler(&m_mqtt) && result;
    result = handler.register_event_handler(&m_heartbeat_timer, [this](auto&) { handle_heartbeat_timer(); }) && result;
    return result;
}

bool gpio_bridge::unregister_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = handler.unregister_event_handler(m_udp.get());
    result = handler.unregister_event_handler(&m_mqtt) && result;
    result = handler.unregister_event_handler(&m_heartbeat_timer) && result;
    return result;
}

void gpio_bridge::dispatch(everest::lib::io::mqtt::mqtt_client::message const& data) {
    auto& topic = data.topic;
    auto& payload = data.payload;
    auto operation = utilities::string_after_pattern(topic, m_receive_topic);
    uint16_t value = 0;
    int id = 0;

    auto stous = [](std::string const& data) {
        auto val = stoi(data);
        if (val < 0 or val > std::numeric_limits<uint16_t>::max()) {
            throw std::range_error("");
        }
        return static_cast<uint16_t>(val);
    };

    try {
        value = stous(payload);
    } catch (...) {
        std::cout << "INVALID DATA on MQTT for GPIO DATA" << std::endl;
        return;
    }
    try {
        id = std::stoi(operation);
    } catch (...) {
        std::cout << "INVALID DATA on MQTT for GPIO ID" << std::endl;
        return;
    }
    if (id < 0 or id >= CB_NUMBER_OF_GPIOS) {
        std::cout << "INVALID GPIO ID" << std::endl;
        return;
    }

    m_message.data.gpio_values[id] = value;
    send_udp();
}

void gpio_bridge::send_mqtt(std::string const& topic, std::string const& message) {
    everest::lib::io::mqtt::mqtt_client::message payload;
    payload.topic = m_send_topic + topic;
    payload.payload = message;
    m_mqtt.publish(payload);
}

void gpio_bridge::send_udp() {
    if (not m_udp_on_error && m_udp) {
        everest::lib::io::udp::udp_payload payload;
        utilities::struct_to_vector(m_message, payload.buffer);
        m_udp->tx(payload);
    }
}

void gpio_bridge::handle_error_timer() {
    if (m_udp_on_error) {
        if (m_udp) {
            m_udp->reset();
        }
    }
}

void gpio_bridge::handle_heartbeat_timer() {
    send_udp();
}

void gpio_bridge::handle_udp_rx(everest::lib::io::udp::udp_payload const& payload) {
    CbManagementPacket<CbGpioPacket> data;
    if (payload.size() == sizeof(data)) {
        std::memcpy(&data, payload.buffer.data(), sizeof(data));
        for (std::size_t i = 0; i < sizeof(data.data.gpio_values) / sizeof(data.data.gpio_values[0]); ++i) {
            send_mqtt(std::to_string(i), std::to_string(data.data.gpio_values[i]));
        }
    } else {
        std::cout << "INVALID DATA SIZE in UDP RX of GPIO: " << payload.size() << " vs " << sizeof(data) << std::endl;
    }
}

void gpio_bridge::handle_ready() {
    m_ready.set(m_udp_ready and m_mqtt_ready and m_cb_is_connected);
}

bool gpio_bridge::available() const {
    return m_ready;
}

void gpio_bridge::set_cb_connection_status(bool connected) {
    m_cb_is_connected.set(connected);
}

} // namespace charge_bridge
