// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest
#include <charge_bridge/can_bridge.hpp>
#include <charge_bridge/utilities/logging.hpp>
#include <chrono>
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

void msg_cb_to_host(cb_can_message const& src, everest::lib::io::can::can_dataset& tar) {
    tar.set_can_id_with_flags(src.can_id, src.can_flags & CanFlags_EFF, src.can_flags & CanFlags_RTR,
                              src.can_flags & CanFlags_ERR);
    tar.len8_dlc = 0;
    tar.payload.resize(src.dlc);
    std::memcpy(tar.payload.data(), src.data, src.dlc);
}

void msg_host_to_cb(everest::lib::io::can::can_dataset const& src, cb_can_message& tar) {
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

can_bridge::can_bridge(can_bridge_config const& config) :
    m_udp(config.cb_remote, config.cb_port, default_udp_timeout_ms),
    m_can_device(config.can_device),
    m_last_msg_to_cb(std::chrono::steady_clock::time_point()) {

    m_identifier = config.cb + "/" + config.item;

    auto& manager = everest::lib::io::netlink::vcan_netlink_manager::Instance();
    auto success = manager.create(config.can_device) && manager.bring_up(config.can_device);
    if (not success) {
        manager.destroy(config.can_device);
        success = manager.create(config.can_device) && manager.bring_up(config.can_device);
        if (not success) {
            manager.destroy(config.can_device);
            throw std::runtime_error("Failed to setup virtual CAN device: " + config.can_device);
        }
    }

    if (not m_can.open(config.can_device)) {
        manager.destroy(config.can_device);
        throw std::runtime_error("Failed to open CAN socket on: " + config.can_device);
    }

    m_udp.set_rx_handler([this](auto const& data, auto&) {
        everest::lib::io::can::can_dataset pl;
        cb_can_message msg;
        std::memcpy(&msg, data.buffer.data(), sizeof(cb_can_message));

        msg_cb_to_host(msg, pl);
        if (is_data_msg(msg)) {
            if (not m_can.tx(pl)) {
                auto const err = m_can.get_error();
                utilities::print_error(m_identifier, "CAN/HW", err ? err : EIO)
                    << "Failed to write frame to " << m_can_device << std::endl;
            }
        }
    });

    m_udp.set_error_handler([this](auto id, auto const& msg) {
        utilities::print_error(m_identifier, "CAN/UDP", id) << msg << std::endl;
        if (id not_eq 0) {
            m_udp.reset();
        }
    });

    m_heartbeat_timer.set_timeout(10s);
    m_can_poll_timer.set_timeout(50ms);
}

can_bridge::~can_bridge() {
    auto& manager = everest::lib::io::netlink::vcan_netlink_manager::Instance();
    m_can.close();
    manager.destroy(m_can_device);
}

void can_bridge::drain_can_from_vcan() {
    if (not m_can.is_open()) {
        return;
    }

    everest::lib::io::can::can_dataset frame;
    while (m_can.rx(frame)) {
        cb_can_message msg;
        msg_host_to_cb(frame, msg);
        if (is_data_msg(msg)) {
            send_can_to_udp(msg);
        }
    }
}

bool can_bridge::register_events(everest::lib::io::event::fd_event_handler& handler) {
    using everest::lib::io::event::poll_events;

    bool can_registered = false;
    if (m_can.is_open()) {
        m_registered_can_fd = m_can.get_fd();
        can_registered = handler.register_event_handler(
            m_registered_can_fd, [this](auto const&) { drain_can_from_vcan(); }, poll_events::read);
    }

    auto udp_registered = handler.register_event_handler(&m_udp);
    auto timer_registered =
        handler.register_event_handler(&m_heartbeat_timer, [this](auto&) { handle_heartbeat_timer(); });
    auto poll_registered =
        handler.register_event_handler(&m_can_poll_timer, [this](auto&) { drain_can_from_vcan(); });

    auto result = can_registered && udp_registered && timer_registered && poll_registered;

    if (result) {
        handler.add_action([this]() {
            drain_can_from_vcan();
            handle_heartbeat_timer();
        });
    }

    return result;
}

bool can_bridge::unregister_events(everest::lib::io::event::fd_event_handler& handler) {
    auto result = true;
    if (m_registered_can_fd >= 0) {
        result = handler.unregister_event_handler(m_registered_can_fd) && result;
        m_registered_can_fd = -1;
    }
    result = handler.unregister_event_handler(&m_udp) && result;
    result = handler.unregister_event_handler(&m_heartbeat_timer) && result;
    result = handler.unregister_event_handler(&m_can_poll_timer) && result;
    return result;
}

void can_bridge::send_can_to_udp(cb_can_message const& msg) {
    everest::lib::io::udp::udp_client::ClientPayloadT udp_pl;
    udp_pl.buffer.resize(sizeof(cb_can_message));
    std::memcpy(udp_pl.buffer.data(), &msg, sizeof(cb_can_message));
    m_udp.tx(udp_pl);
    m_last_msg_to_cb = std::chrono::steady_clock::now();
}

void can_bridge::handle_heartbeat_timer() {
    drain_can_from_vcan();

    if (m_udp.on_error()) {
        m_heartbeat_timer.set_timeout(250ms);
        m_last_msg_to_cb = std::chrono::steady_clock::time_point();
        return;
    }
    m_heartbeat_timer.set_timeout(10s);

    auto delta = std::chrono::steady_clock::now() - m_last_msg_to_cb;
    if (delta > 10s) {
        cb_can_message msg = cb_can_message_set_zero;
        msg.packet_type = CanPacketType_Keep_Alive;
        send_can_to_udp(msg);
    }
}

} // namespace charge_bridge
