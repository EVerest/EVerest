// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "protocol/cb_management.h"
#include <chrono>
#include <everest/io/can/can_payload.hpp>
#include <everest/io/event/fd_event_register_interface.hpp>
#include <everest/io/event/timer_fd.hpp>
#include <everest/io/udp/udp_client.hpp>
#include <everest/util/misc/observable.hpp>
#include <memory>
#include <protocol/cb_config.h>

namespace charge_bridge {

struct heartbeat_config {
    std::string cb;
    std::string item;
    std::uint16_t cb_port;
    std::string cb_remote;
    std::uint16_t interval_s;
    std::uint16_t connection_to_s;
    CbConfig cb_config;
};

class heartbeat_service : public everest::lib::io::event::fd_event_register_interface {
public:
    heartbeat_service(heartbeat_config const& config, std::function<void(bool)> const& publish_connection_status,
                      everest::lib::io::event::event_fd& ready_notify);
    ~heartbeat_service();

    bool register_events(everest::lib::io::event::fd_event_handler& handler) override;
    bool unregister_events(everest::lib::io::event::fd_event_handler& handler) override;
    void disconnect_cb_endpoint();
    void connect_cb_endpoint(std::string const& remote);
    bool available() const;
    int mcu_reset_count() const;

private:
    void handle_error_timer();
    void handle_heartbeat_timer();
    void handle_udp_rx(everest::lib::io::udp::udp_payload const& payload);
    void create_udp_client(std::string const& remote, uint16_t remote_port);
    void handle_ready();

    std::unique_ptr<everest::lib::io::udp::udp_client> m_udp;
    std::uint16_t m_udp_port{0};
    std::string m_udp_remote;
    bool m_udp_on_error{false};
    bool m_udp_ready{false};
    everest::lib::io::event::timer_fd m_heartbeat_timer;
    std::string m_identifier;
    CbManagementPacket<CbConfig> m_config_message;
    std::chrono::steady_clock::time_point m_last_heartbeat_reply;
    bool m_cb_connected{false};
    bool m_inital_cb_commcheck{true};
    std::chrono::milliseconds m_heartbeat_interval;
    std::chrono::milliseconds m_connection_to;
    std::function<void(bool)> m_publish_connection_status;
    std::uint32_t m_mcu_timestamp{0};
    int m_mcu_reset_count{0};
    everest::lib::util::observable<bool> m_ready{false};
    everest::lib::io::event::event_fd& m_ready_notify;
};

} // namespace charge_bridge
