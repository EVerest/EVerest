// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#include <iostream>
#include <vector>

#include "CanBus.hpp"
#include <everest/io/can/can_recv_filter.hpp>
#include <everest/logging.hpp>

using namespace std::chrono_literals;

namespace {
constexpr auto CAN_RECOVERY_TIMER_INTERVAL = 1000ms;
constexpr auto CAN_POLL_STATUS_TIMER_INTERVAL = 1000ms;
// InfyPower protocol: space controller commands 50–200 ms apart.
constexpr auto CAN_PACE_TX_INTERVAL = 50ms;

constexpr uint32_t INFY_INNER_FRAME_ID = 0x0757F800;
constexpr uint32_t INFY_INNER_FRAME_MASK = 0x1FFFF800;

std::vector<everest::lib::io::can::can_recv_filter> infy_kernel_recv_filters() {
    return {everest::lib::io::can::can_recv_filter::reject_match(INFY_INNER_FRAME_ID, INFY_INNER_FRAME_MASK)};
}
} // namespace

CanBus::CanBus() : rx_thread_online{true}, can_bus(nullptr) {
}

CanBus::~CanBus() {
    close_device();
}

bool CanBus::open_device(const std::string& dev) {
    can_bus = std::make_unique<can::socket_can>(dev, infy_kernel_recv_filters());
    can_bus->set_rx_handler([&](auto const& pl, auto&) {
        uint32_t can_id = pl.get_can_id_with_flags();
        this->rx_handler(can_id, pl.payload);
    });
    can_bus->set_error_handler([&](auto err, auto msg) {
        if (err != 0) {
            EVLOG_error << "CAN error: " << err << " - " << msg << std::endl;
            on_error.store(true);
        } else {
            EVLOG_info << "CAN error cleared: " << msg << std::endl;
            on_error.store(false);
        }
    });
    ev_handler.register_event_handler(can_bus.get());
    recovery_timer.set_timeout(CAN_RECOVERY_TIMER_INTERVAL);

    ev_handler.register_event_handler(&recovery_timer, [&](event::fd_event_handler::event_list const& events) {
        if (on_error.load()) {
            EVLOG_error << "CAN error detected, attempting recovery";
            can_bus->reset();
        }
    });
    poll_status_timer.set_timeout(CAN_POLL_STATUS_TIMER_INTERVAL);

    ev_handler.register_event_handler(
        &poll_status_timer, [&](event::fd_event_handler::event_list const& events) { poll_status_handler(); });

    pace_tx_timer.set_single_shot(true);
    pace_tx_timer.disarm();
    ev_handler.register_event_handler(&pace_tx_timer,
                                      [&](event::fd_event_handler::event_list const& events) { pace_tx_handler(); });

    rx_thread_handle = std::thread(&CanBus::rx_thread, this);
    return true;
}

bool CanBus::close_device() {
    if (!can_bus) {
        return true;
    }

    EVLOG_info << "Closing CAN device";

    rx_thread_online = false;
    if (rx_thread_handle.joinable()) {
        rx_thread_handle.join();
    }

    ev_handler.unregister_event_handler(&recovery_timer);
    ev_handler.unregister_event_handler(&poll_status_timer);
    ev_handler.unregister_event_handler(&pace_tx_timer);
    ev_handler.unregister_event_handler(can_bus.get());

    clear_paced_tx_queue();

    can_bus.reset();
    on_error.store(false);

    EVLOG_info << "CAN device closed successfully";
    return true;
}

void CanBus::rx_thread() {
    EVLOG_info << "Starting CAN RX thread" << std::endl;
    ev_handler.run(rx_thread_online);
}

void CanBus::enqueue_paced_tx(uint32_t can_id, std::vector<uint8_t> payload) {
    m_pace_tx_queue.push_back(paced_tx_frame{can_id, std::move(payload)});
}

void CanBus::clear_paced_tx_queue() {
    m_pace_tx_queue.clear();
    disarm_pace_tx_timer();
}

void CanBus::start_paced_tx_cycle() {
    disarm_pace_tx_timer();

    if (m_pace_tx_queue.empty()) {
        return;
    }

    auto frame = std::move(m_pace_tx_queue.front());
    m_pace_tx_queue.pop_front();
    _tx(frame.can_id, frame.payload);

    if (!m_pace_tx_queue.empty()) {
        arm_pace_tx_one_shot();
    }
}

void CanBus::pace_tx_handler() {
    pace_tx_timer.read();

    if (m_pace_tx_queue.empty()) {
        disarm_pace_tx_timer();
        return;
    }

    auto frame = std::move(m_pace_tx_queue.front());
    m_pace_tx_queue.pop_front();
    _tx(frame.can_id, frame.payload);

    if (!m_pace_tx_queue.empty()) {
        arm_pace_tx_one_shot();
    } else {
        disarm_pace_tx_timer();
    }
}

bool CanBus::arm_pace_tx_one_shot() {
    return pace_tx_timer.set_timeout(CAN_PACE_TX_INTERVAL);
}

void CanBus::disarm_pace_tx_timer() {
    pace_tx_timer.disarm();
}

bool CanBus::_tx(uint32_t can_id, const std::vector<uint8_t>& payload) {
    if (payload.size() > 8) {
        EVLOG_error << "CAN payload too large (" << payload.size() << " bytes), max 8 bytes allowed";
        return false;
    }

    everest::lib::io::can::can_dataset data;
    // Use plain 29-bit id only; EFF must not be OR'd into the arbitration field.
    data.set_can_id_with_flags(can_id & CAN_EFF_MASK, true, false, false);
    data.payload = payload;

    if (on_error.load()) {
        EVLOG_error << "CAN error detected, not sending frame";
        return false;
    }
    return can_bus->tx(data);
}
