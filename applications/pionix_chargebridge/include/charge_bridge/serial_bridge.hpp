// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <charge_bridge/utilities/symlink.hpp>
#include <everest/io/event/fd_event_register_interface.hpp>
#include <everest/io/event/timer_fd.hpp>
#include <everest/io/serial/event_pty.hpp>
#include <everest/io/tcp/tcp_client.hpp>
#include <everest/util/misc/observable.hpp>

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
    std::string get_slave_path();
    bool available() const;

private:
    void reset_tcp();
    void handle_ready();

    everest::lib::io::serial::event_pty m_pty;
    everest::lib::io::tcp::tcp_client m_tcp;
    utilities::symlink m_symlink;
    int m_tcp_last_error_id = -1;
    bool m_tcp_ready{false};
    bool m_pty_ready{false};
    everest::lib::util::observable<bool> m_ready{false};
    everest::lib::io::event::event_fd& m_ready_notify;
};

} // namespace charge_bridge
