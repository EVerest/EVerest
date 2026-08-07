// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <charge_bridge/utilities/symlink.hpp>
#include <everest/io/event/fd_event_register_interface.hpp>
#include <everest/io/event/timer_fd.hpp>
#include <everest/io/serial/event_pty.hpp>
#include <everest/io/tcp/tcp_client.hpp>
#include <everest/util/misc/observable.hpp>
#include <memory>

namespace charge_bridge {

struct serial_bridge_config {
    std::string cb;
    std::string item;
    std::uint16_t cb_port;
    std::string cb_remote;
    std::string serial_device;
};

class serial_bridge : public everest::lib::io::event::fd_event_register_interface {
public:
    serial_bridge(serial_bridge_config const& config, everest::lib::io::event::event_fd& ready_notify);
    ~serial_bridge() = default;

    bool register_events(everest::lib::io::event::fd_event_handler& handler) override;
    bool unregister_events(everest::lib::io::event::fd_event_handler& handler) override;
    void disconnect_cb_endpoint();
    void connect_cb_endpoint(std::string const& remote);
    std::string get_slave_path();
    bool available() const;

private:
    void create_tcp_client(std::string const& remote, uint16_t remote_port);
    void handle_ready();

    everest::lib::io::serial::event_pty m_pty;
    std::unique_ptr<everest::lib::io::tcp::tcp_client> m_tcp;
    utilities::symlink m_symlink;
    std::string m_identifier;
    int m_tcp_last_error_id = -1;
    std::uint16_t m_tcp_port{0};
    std::string m_tcp_remote;
    bool m_tcp_ready{false};
    bool m_pty_ready{false};
    everest::lib::util::observable<bool> m_ready{false};
    everest::lib::io::event::event_fd& m_ready_notify;
};

} // namespace charge_bridge
