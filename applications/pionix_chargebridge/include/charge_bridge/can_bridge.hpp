// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest
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
    std::unique_ptr<everest::lib::io::can::socket_can> m_can;
    std::unique_ptr<everest::lib::io::udp::udp_client> m_udp;
    uint16_t m_cb_port{0};
    std::string m_cb_remote;
    std::string m_can_device;
    std::string m_identifier;
    everest::lib::io::event::timer_fd m_heartbeat_timer;
    std::chrono::steady_clock::time_point m_last_msg_to_cb;
    bool m_udp_ready{false};
    bool m_can_ready{false};
    everest::lib::util::observable<bool> m_ready{false};
    everest::lib::io::event::event_fd& m_ready_notify;
    everest::lib::util::observable<bool> m_cb_is_connected{false};
};

} // namespace charge_bridge
