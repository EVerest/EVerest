// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest
#include <charge_bridge/plc_bridge.hpp>
#include <charge_bridge/utilities/logging.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/udp/udp_payload.hpp>
#include <iostream>

namespace {
const int default_udp_timeout_ms = 1000;
} // namespace

namespace charge_bridge {

plc_bridge::plc_bridge(plc_bridge_config const& config, everest::lib::io::event::event_fd& ready_notify) :
    m_tap(config.plc_tap, config.plc_ip, config.plc_netmaks, config.plc_mtu),
    m_udp_port(config.cb_port),
    m_udp_remote(config.cb_remote),
    m_ready_notify(ready_notify) {

    using namespace std::chrono_literals;
    m_timer.set_timeout(5s);

    auto identifier = config.cb + "/" + config.item;
    m_identifier = identifier;
    m_tap.set_rx_handler([this](auto const& data, auto&) {
        everest::lib::io::udp::udp_payload pl;
        pl.buffer = data;
        if (m_udp) {
            m_udp->tx(pl);
        }
    });

    create_udp_client(config.cb_remote, config.cb_port, identifier);

    m_tap.set_error_handler([this, identifier](auto id, auto const& msg) {
        utilities::print_error(identifier, "PLC/TAP", id) << msg << std::endl;
        m_tap_on_error = id not_eq 0;
        m_tap_ready = id == 0;
        handle_ready();
    });
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

void plc_bridge::create_udp_client(std::string const& remote, uint16_t remote_port, std::string const& identifier) {
    m_udp = std::make_unique<everest::lib::io::udp::udp_client>(remote, remote_port, default_udp_timeout_ms);
    m_udp_ready = false;
    m_udp_on_error = false;
    m_udp->set_rx_handler([this](auto const& data, auto&) { m_tap.tx(data.buffer); });
    m_udp->set_error_handler([this, identifier](auto id, auto const& msg) {
        utilities::print_error(identifier, "PLC/UDP", id) << msg << std::endl;
        m_udp_on_error = id not_eq 0;
        m_udp_ready = id == 0;
        handle_ready();
    });
}

void plc_bridge::disconnect_cb_endpoint() {
    m_udp_ready = false;
    m_udp_on_error = false;
    m_udp.reset();
    handle_ready();
}

void plc_bridge::connect_cb_endpoint(std::string const& remote) {
    m_udp_remote = remote;
    disconnect_cb_endpoint();
    create_udp_client(m_udp_remote, m_udp_port, m_identifier);
    handle_ready();
}

void plc_bridge::handle_timer_event() {
    if (m_udp_on_error) {
        if (m_udp) {
            m_udp->reset();
        }
    }
    if (m_tap_on_error) {
        m_tap.reset();
    }
}

void plc_bridge::handle_ready() {
    m_ready.set(m_udp_ready and m_tap_ready and m_cb_is_connected);
}

bool plc_bridge::available() const {
    return m_ready;
}

void plc_bridge::set_cb_connection_status(bool connected) {
    m_cb_is_connected.set(connected);
}

bool plc_bridge::register_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    result = handler.register_event_handler(&m_tap) && result;
    result = handler.register_event_handler(m_udp.get()) && result;
    result = handler.register_event_handler(&m_timer, [this](auto) { handle_timer_event(); }) && result;
    return result;
}

bool plc_bridge::unregister_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    result = handler.unregister_event_handler(&m_tap) && result;
    result = handler.unregister_event_handler(m_udp.get()) && result;
    result = handler.unregister_event_handler(&m_timer) && result;
    return result;
}

} // namespace charge_bridge
