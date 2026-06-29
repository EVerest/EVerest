// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "everest/io/mqtt/mosquitto_cpp.hpp"
#include <array>
#include <everest/io/can/can_payload.hpp>
#include <everest/io/event/fd_event_register_interface.hpp>
#include <everest/io/event/timer_fd.hpp>
#include <everest/io/mqtt/mqtt_client.hpp>
#include <everest/io/udp/udp_client.hpp>
#include <everest/util/misc/observable.hpp>
#include <optional>
#include <protocol/cb_management.h>
#include <string>
#include <utility>
#include <vector>

namespace charge_bridge {

// Latest decoded contents of a received CbIoPacket, for display in the terminal UI.
struct io_state {
    std::vector<int> gpio;                              // GPIO input values (0/1 or PWM duty)
    std::vector<int> adc;                               // calibrated ADC values
    std::vector<std::pair<std::string, int>> telemetry; // unstructured name -> value entries
};

// Combined GPIO + ADC bridge. Owns one UDP connection to the ChargeBridge:
//  - sends GPIO output writes (CST_HostToCb_Gpio), which also keeps the connection
//    alive so the MCU keeps pushing to this endpoint;
//  - receives the combined CbIoPacket (CST_CbToHost_Io) and republishes GPIO inputs
//    and calibrated ADC values to MQTT.
struct io_config {
    std::string cb;
    std::string item;
    std::uint16_t cb_port;
    std::string cb_remote;
    std::uint16_t interval_s;
    std::string mqtt_remote;
    std::string mqtt_bind;
    std::uint16_t mqtt_port;
    std::uint32_t mqtt_ping_interval_ms;
};

class io_bridge : public everest::lib::io::event::fd_event_register_interface {
public:
    io_bridge(io_config const& config, everest::lib::io::event::event_fd& ready_notify);
    ~io_bridge();

    bool register_events(everest::lib::io::event::fd_event_handler& handler) override;
    bool unregister_events(everest::lib::io::event::fd_event_handler& handler) override;
    void disconnect_cb_endpoint();
    void connect_cb_endpoint(std::string const& remote);
    bool available() const;
    void set_cb_connection_status(bool connected);
    // Latest decoded GPIO/ADC/telemetry snapshot; empty until the first valid IO packet arrives.
    std::optional<io_state> latest_io() const;

private:
    void handle_error_timer();
    void handle_heartbeat_timer();
    void handle_ready();
    void create_udp_client(std::string const& remote, uint16_t remote_port);
    void handle_udp_rx(everest::lib::io::udp::udp_payload const& payload);
    void dispatch(everest::lib::io::mqtt::mqtt_client::message const& data);
    void dispatch_ws28(everest::lib::io::mqtt::mqtt_client::message const& data);
    void dispatch_ws28_anim(everest::lib::io::mqtt::mqtt_client::message const& data);
    void send_mqtt(std::string const& topic, std::string const& message);
    void send_adc_mqtt(std::string const& topic, std::string const& message);
    void send_telemetry_mqtt(std::string const& topic, std::string const& message);
    void send_udp();
    void send_ws28_udp();
    void send_ws28_anim_udp();

    std::unique_ptr<everest::lib::io::udp::udp_client> m_udp;
    std::uint16_t m_udp_port{0};
    std::string m_udp_remote;
    bool m_udp_on_error{false};
    bool m_udp_ready{false};
    everest::lib::io::event::timer_fd m_heartbeat_timer;
    std::chrono::steady_clock::time_point last_heartbeat;
    CbManagementPacket<CbGpioPacket> m_message;
    CbManagementPacket<CbWs28Packet> m_ws28_message;
    CbManagementPacket<CbWs28AnimPacket> m_ws28_anim_message;
    std::string m_identifier;
    bool m_mqtt_on_error{false};
    bool m_mqtt_ready{false};
    everest::lib::io::mqtt::mqtt_client m_mqtt;
    std::string m_receive_topic;
    std::string m_ws28_receive_topic;
    std::string m_ws28_anim_receive_topic;
    std::string m_send_topic;
    std::string m_adc_send_topic;
    std::string m_telemetry_send_topic;
    everest::lib::util::observable<bool> m_ready{false};
    everest::lib::util::observable<bool> m_cb_is_connected{false};
    io_state m_io_state;
    bool m_have_io{false};
    everest::lib::io::event::event_fd& m_ready_notify;

    // Reassembly buffer for the MCU debug-UART byte stream (CST_CbToHost_DebugUart). Chunks arrive
    // split at arbitrary byte boundaries, so we accumulate and emit one log entry per '\n'.
    std::string m_mcu_log_partial;
};

} // namespace charge_bridge
