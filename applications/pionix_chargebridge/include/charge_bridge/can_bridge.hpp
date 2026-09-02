// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include "everest/io/event/event_fd.hpp"
#include <chrono>
#include <everest/io/can/can_payload.hpp>
#include <everest/io/can/socket_can.hpp>
#include <everest/io/event/fd_event_register_interface.hpp>
#include <everest/io/event/timer_fd.hpp>
#include <everest/io/udp/udp_client.hpp>
#include <everest/util/misc/observable.hpp>
#include <memory>

extern "C" struct cb_can_message;

namespace charge_bridge {

struct can_bridge_config {
    std::string cb;
    std::string item;
    std::uint16_t cb_port;
    std::string cb_remote;
    std::string can_device;
    // CB CAN bitrate in bit/s from the heartbeat config; 0 = unknown, pacing off.
    std::uint32_t can_bitrate_bps{0};
};

class can_bridge : public everest::lib::io::event::fd_event_register_interface {
public:
    can_bridge(can_bridge_config const& config, everest::lib::io::event::event_fd& ready_notify);
    ~can_bridge();

    bool register_events(everest::lib::io::event::fd_event_handler& handler) override;
    bool unregister_events(everest::lib::io::event::fd_event_handler& handler) override;
    void disconnect_cb_endpoint();
    void connect_cb_endpoint(std::string const& remote);
    bool available() const;
    void set_cb_connection_status(bool connected);

private:
    void handle_heartbeat_timer();
    void handle_ready();
    void create_udp_client(std::string const& remote, uint16_t remote_port);
    void send_can_to_udp(cb_can_message const& pl);
    void append_to_can_batch(everest::lib::io::can::socket_can::ClientPayloadT const& frame);
    void flush_can_batch();
    // Conservative wire size of a classic CAN frame, for pacing.
    static std::size_t frame_wire_bits(cb_can_message const& msg);
    void reset_pacing();
    bool install_vcan_rate_limit(can_bridge_config const& config);
    void update_vcan_rate_limit();
    std::uint64_t vcan_rate_for(double avg_wire_bits) const;
    std::unique_ptr<everest::lib::io::can::socket_can> m_can;
    std::unique_ptr<everest::lib::io::udp::udp_client> m_udp;
    uint16_t m_cb_port{0};
    std::string m_cb_remote;
    std::string m_can_device;
    std::string m_identifier;
    everest::lib::io::event::timer_fd m_heartbeat_timer;
    // CAN->UDP batch: flushed when full or on the hold-off timer.
    everest::lib::io::udp::udp_client::ClientPayloadT m_can_batch;
    everest::lib::io::event::timer_fd m_can_batch_timer;
    bool m_can_batch_timer_armed{false};
    // Pacing token bucket in bits, refilled at m_pace_bitrate_bps * pace_utilisation.
    std::uint32_t m_pace_bitrate_bps{0};
    double m_pace_tokens_bits{0.0};
    std::chrono::steady_clock::time_point m_pace_last{};
    std::size_t m_can_batch_bits{0};
    bool m_can_rx_paused{false};
    // True once the tbf qdisc is on the vcan; the in-bridge pacer then stands down.
    bool m_vcan_shaped{false};
    std::uint64_t m_vcan_rate_bps{0}; // tbf rate currently installed
    std::chrono::steady_clock::time_point m_vcan_rate_updated{};
    double m_avg_wire_bits{0.0}; // EWMA of real wire bits per forwarded frame
    std::chrono::steady_clock::time_point m_last_msg_to_cb;
    bool m_udp_ready{false};
    bool m_can_ready{false};
    everest::lib::util::observable<bool> m_ready{false};
    everest::lib::io::event::event_fd& m_ready_notify;
    everest::lib::util::observable<bool> m_cb_is_connected{false};
};

} // namespace charge_bridge
