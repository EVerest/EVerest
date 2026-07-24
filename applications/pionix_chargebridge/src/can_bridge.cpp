// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest
#include <charge_bridge/can_bridge.hpp>
#include <charge_bridge/utilities/logging.hpp>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/netlink/vcan_netlink_manager.hpp>
#include <memory>
#include <protocol/cb_can_message.h>

namespace {
const int default_udp_timeout_ms = 1000;
}

namespace charge_bridge {
using namespace std::chrono_literals;

namespace {

void msg_cb_to_host(cb_can_message const& src, everest::lib::io::can::socket_can::ClientPayloadT& tar) {
    tar.set_can_id_with_flags(src.can_id, src.can_flags & CanFlags_EFF, src.can_flags & CanFlags_RTR,
                              src.can_flags & CanFlags_ERR);
    tar.len8_dlc = 0;
    // dlc is wire data: clamp to the data array size so a bogus value can't over-read past src.data.
    auto const len = std::min<std::size_t>(src.dlc, sizeof(src.data));
    tar.payload.resize(len);
    std::memcpy(tar.payload.data(), src.data, len);
}

void msg_host_to_cb(everest::lib::io::can::socket_can::ClientPayloadT const& src, cb_can_message& tar) {
    tar = cb_can_message_set_zero;
    tar.can_id = src.get_can_id();
    tar.can_flags = 0;
    if (src.eff_flag()) {
        tar.can_flags |= CanFlags_EFF;
    }
    if (src.rtr_flag()) {
        tar.can_flags |= CanFlags_RTR;
    }
    if (src.err_flag()) {
        tar.can_flags |= CanFlags_ERR;
    }
    tar.dlc = std::min<uint8_t>(src.payload.size(), sizeof(tar.data));
    std::memcpy(tar.data, src.payload.data(), src.payload.size());
}

bool is_data_msg([[maybe_unused]] cb_can_message const& msg) {
    return true;
}

} // namespace

can_bridge::can_bridge(can_bridge_config const& config, everest::lib::io::event::event_fd& ready_notify) :
    m_cb_port(config.cb_port),
    m_cb_remote(config.cb_remote),
    m_can_device(config.can_device),
    m_last_msg_to_cb(std::chrono::steady_clock::time_point()),
    m_ready_notify(ready_notify) {

    auto& manager = everest::lib::io::netlink::vcan_netlink_manager::Instance();
    auto success = manager.create(config.can_device) && manager.bring_up(config.can_device);
    if (success) {
        m_can = std::make_unique<everest::lib::io::can::socket_can>(config.can_device);
    } else {
        manager.destroy(config.can_device);
        success = manager.create(config.can_device) && manager.bring_up(config.can_device);
        if (success) {
            m_can = std::make_unique<everest::lib::io::can::socket_can>(config.can_device);
        } else {
            manager.destroy(config.can_device);
            throw std::runtime_error("Failed to setup virtual CAN device: " + config.can_device);
        }
    }

    m_can->set_rx_handler([this](auto const& data, auto&) {
        everest::lib::io::udp::udp_client::ClientPayloadT pl;
        cb_can_message msg;
        msg_host_to_cb(data, msg);
        send_can_to_udp(msg);
    });

    create_udp_client(config.cb_remote, config.cb_port);

    auto identifier = config.cb + "/" + config.item;
    m_identifier = identifier;
    m_can->set_error_handler([this](auto id, auto const& msg) {
        utilities::print_error(m_identifier, "CAN/HW", id) << msg << std::endl;
        m_can_ready = id == 0;
        if (not m_can_ready) {
            // This is a smart pointer!! Using .reset() would delete the obj!
            m_can->reset();
        }
        handle_ready();
    });
    m_heartbeat_timer.set_timeout(10s);
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

void can_bridge::create_udp_client(std::string const& remote, uint16_t remote_port) {
    m_udp = std::make_unique<everest::lib::io::udp::udp_client>(remote, remote_port, default_udp_timeout_ms);
    m_udp->set_rx_handler([this](auto const& data, auto&) {
        // The MCU may omit trailing data bytes (see cb_can_message: "data bytes at the end may be
        // omitted"), so the datagram can be shorter than the full struct but must at least cover the
        // fixed header. Zero-init and copy only the bytes actually received to avoid reading past
        // the receive buffer.
        static constexpr auto header_size = offsetof(cb_can_message, data);
        if (data.size() < header_size || data.size() > sizeof(cb_can_message)) {
            return;
        }
        everest::lib::io::can::socket_can::ClientPayloadT pl;
        cb_can_message msg = cb_can_message_set_zero;
        std::memcpy(&msg, data.buffer.data(), data.size());

        msg_cb_to_host(msg, pl);
        if (is_data_msg(msg)) {
            m_can->tx(pl);
        }
    });
    m_udp->set_error_handler([this](auto id, auto const& msg) {
        utilities::print_error(m_identifier, "CAN/UDP", id) << msg << std::endl;
        m_udp_ready = id == 0;
        if (not m_udp_ready) {
            if (m_udp) {
                m_udp->reset();
            }
        }
        handle_ready();
    });
}

void can_bridge::disconnect_cb_endpoint() {
    m_udp_ready = false;
    m_last_msg_to_cb = std::chrono::steady_clock::time_point{};
    if (m_udp) {
        m_udp->reset();
    }
    m_udp.reset();
    handle_ready();
}

void can_bridge::connect_cb_endpoint(std::string const& remote) {
    m_cb_remote = remote;
    disconnect_cb_endpoint();
    create_udp_client(m_cb_remote, m_cb_port);
    handle_ready();
}

can_bridge::~can_bridge() {
    auto& manager = everest::lib::io::netlink::vcan_netlink_manager::Instance();
    if (m_can) {
        m_can.reset();
        manager.destroy(m_can_device);
    }
}

bool can_bridge::register_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = handler.register_event_handler(m_can.get());
    result = handler.register_event_handler(m_udp.get()) && result;
    result = handler.register_event_handler(&m_heartbeat_timer, [this](auto&) { handle_heartbeat_timer(); }) && result;

    if (result) {
        handler.add_action([this]() { handle_heartbeat_timer(); });
    }

    return result;
}

bool can_bridge::unregister_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = handler.unregister_event_handler(m_can.get());
    result = handler.unregister_event_handler(m_udp.get()) && result;
    result = handler.unregister_event_handler(&m_heartbeat_timer) && result;
    return result;
}

void can_bridge::send_can_to_udp(cb_can_message const& msg) {
    if (not m_udp) {
        return;
    }
    everest::lib::io::udp::udp_client::ClientPayloadT udp_pl;
    udp_pl.buffer.resize(sizeof(cb_can_message));
    std::memcpy(udp_pl.buffer.data(), &msg, sizeof(cb_can_message));
    m_udp->tx(udp_pl);
    m_last_msg_to_cb = std::chrono::steady_clock::now();
}

void can_bridge::handle_heartbeat_timer() {
    if (not m_udp or m_udp->on_error()) {
        // If the connection is not available, retry soon and invalidate last hearbeat
        m_heartbeat_timer.set_timeout(250ms);
        m_last_msg_to_cb = std::chrono::steady_clock::time_point();
        return;
    } else {
        // otherwise go back to regular interval
        m_heartbeat_timer.set_timeout(10s);
    }
    auto delta = std::chrono::steady_clock::now() - m_last_msg_to_cb;
    if (delta > 10s) {
        cb_can_message msg = cb_can_message_set_zero;
        msg.packet_type = CanPacketType_Keep_Alive;
        send_can_to_udp(msg);
    }
}

void can_bridge::handle_ready() {
    m_ready.set(m_udp_ready and m_can_ready and m_cb_is_connected);
}

bool can_bridge::available() const {
    return m_ready;
}

void can_bridge::set_cb_connection_status(bool connected) {
    m_cb_is_connected.set(connected);
}

} // namespace charge_bridge
