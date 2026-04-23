// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include <charge_bridge/adc_bridge.hpp>
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

adc_bridge::adc_bridge(adc_config const& config) :
    m_udp(config.cb_remote, config.cb_port, default_udp_timeout_ms),
    m_mqtt(mqtt_reconnect_timeout_ms)

{
    m_identifier = config.cb + "/" + config.item;

    m_heartbeat_timer.set_timeout(std::chrono::seconds(config.interval_s));

    m_udp.set_rx_handler([this](auto const& data, auto&) { handle_udp_rx(data); });

    m_udp.set_error_handler([this](auto id, auto const& msg) {
        utilities::print_error(m_identifier, "ADC/UDP", id) << msg << std::endl;
        m_udp_on_error = id not_eq 0;
    });

    m_send_topic = "pionix/chargebridge/" + config.cb + "/adc/input/";

    m_mqtt.set_error_handler([this, config](int id, std::string const& msg) {
        utilities::print_error(m_identifier, "GPIO/MQTT", id) << msg << std::endl;
        m_mqtt_on_error = id not_eq 0;
    });

    m_mqtt.connect(config.mqtt_bind, config.mqtt_remote, config.mqtt_port, config.mqtt_ping_interval_ms);
}

adc_bridge::~adc_bridge() {
}

bool adc_bridge::register_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = handler.register_event_handler(&m_udp);
    result = handler.register_event_handler(&m_mqtt) && result;
    result = handler.register_event_handler(&m_heartbeat_timer, [this](auto&) { handle_heartbeat_timer(); }) && result;
    return result;
}

bool adc_bridge::unregister_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = handler.unregister_event_handler(&m_udp);
    result = handler.unregister_event_handler(&m_mqtt) && result;
    result = handler.unregister_event_handler(&m_heartbeat_timer) && result;
    return result;
}

void adc_bridge::send_mqtt(std::string const& topic, std::string const& message) {
    everest::lib::io::mqtt::mqtt_client::message payload;
    payload.topic = m_send_topic + topic;
    payload.payload = message;
    m_mqtt.publish(payload);
}

void adc_bridge::handle_error_timer() {
    if (m_udp_on_error) {
        m_udp.reset();
    }
}

void adc_bridge::handle_heartbeat_timer() {
}

void adc_bridge::handle_udp_rx(everest::lib::io::udp::udp_payload const& payload) {
    CbManagementPacket<CbAdcPacket> data;
    if (payload.size() == sizeof(data)) {
        std::memcpy(&data, payload.buffer.data(), sizeof(data));
        for (std::size_t i = 0; i < sizeof(data.data.adc_values_mV) / sizeof(data.data.adc_values_mV[0]); ++i) {
            send_mqtt(std::to_string(i), std::to_string(data.data.adc_values_mV[i]));
        }
    } else {
        std::cout << "INVALID DATA SIZE in UDP RX of ADC: " << payload.size() << " vs " << sizeof(data) << std::endl;
    }
}

} // namespace charge_bridge
