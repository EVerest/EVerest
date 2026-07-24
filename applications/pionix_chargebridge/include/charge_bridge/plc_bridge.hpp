// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/io/event/fd_event_register_interface.hpp>
#include <everest/io/event/timer_fd.hpp>
#include <everest/io/tun_tap/tap_client.hpp>
#include <everest/io/udp/udp_client.hpp>
#include <everest/util/misc/observable.hpp>
#include <memory>

namespace charge_bridge {

struct plc_bridge_config {
    std::string cb;
    std::string item;
    std::uint16_t cb_port;
    std::string cb_remote;
    std::string plc_tap;
    std::string plc_ip;
    std::string plc_netmaks;
    int plc_mtu;
};

class plc_bridge : public everest::lib::io::event::fd_event_register_interface {
public:
    plc_bridge(plc_bridge_config const& config, everest::lib::io::event::event_fd& ready_notify);
    ~plc_bridge() = default;

    bool register_events(everest::lib::io::event::fd_event_handler& handler) override;
    bool unregister_events(everest::lib::io::event::fd_event_handler& handler) override;
    void disconnect_cb_endpoint();
    void connect_cb_endpoint(std::string const& remote);
    bool available() const;
    void set_cb_connection_status(bool connected);

private:
    void handle_timer_event();
    void handle_ready();
    void create_udp_client(std::string const& remote, uint16_t remote_port, std::string const& identifier);
    everest::lib::io::tun_tap::tap_client m_tap;
    std::unique_ptr<everest::lib::io::udp::udp_client> m_udp;
    everest::lib::io::event::timer_fd m_timer;
    uint16_t m_udp_port{0};
    std::string m_udp_remote;
    std::string m_identifier;
    bool m_udp_on_error{false};
    bool m_tap_on_error{false};
    bool m_udp_ready{false};
    bool m_tap_ready{false};
    everest::lib::util::observable<bool> m_cb_is_connected{false};
    everest::lib::util::observable<bool> m_ready{false};
    everest::lib::io::event::event_fd& m_ready_notify;
};

} // namespace charge_bridge
