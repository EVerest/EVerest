// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <can_device/CanDevice.hpp>

#include <linux/can.h>
#include <net/if.h>

#include <everest/logging.hpp>

using namespace std::chrono_literals;
namespace io = everest::lib::io;

namespace can_device {

namespace {
constexpr auto RECOVERY_INTERVAL = 1000ms;
} // namespace

CanDevice::CanDevice(std::chrono::milliseconds tick_interval) : tick_interval(tick_interval) {
}

CanDevice::~CanDevice() {
    close_device();
}

bool CanDevice::open_device(const std::string& dev) {
    if (dev.empty() or dev.size() >= IFNAMSIZ) {
        EVLOG_error << "CAN interface name is invalid or too long: '" << dev << "'";
        return false;
    }
    if (can_bus) {
        EVLOG_error << "CAN device " << device_name << " is already open";
        return false;
    }
    device_name = dev;

    can_bus = std::make_unique<io::can::socket_can>(dev);
    can_bus->set_rx_handler(
        [this](auto const& data, auto&) { rx_handler(data.get_can_id_with_flags(), data.payload); });
    can_bus->set_error_handler([this](int error, const std::string&) { on_error(error); });
    can_bus->set_on_ready_action([this]() { on_ready(); });

    if (not register_handlers()) {
        EVLOG_error << "CAN device " << device_name << ": registering the event handlers failed";
        unregister_handlers();
        can_bus = nullptr;
        return false;
    }

    ev_thread_online = true;
    ev_thread = std::thread([this]() { ev_handler.run(ev_thread_online); });
    return true;
}

void CanDevice::close_device() {
    if (not can_bus) {
        return;
    }
    // The tick timer wakes the loop, so it notices the stop request within one tick.
    ev_thread_online = false;
    if (ev_thread.joinable()) {
        ev_thread.join();
    }
    unregister_handlers();
    can_bus = nullptr;
    link_up.reset();
}

bool CanDevice::register_handlers() {
    return ev_handler.register_event_handler(can_bus.get()) and recovery_timer.set_timeout(RECOVERY_INTERVAL) and
           ev_handler.register_event_handler(&recovery_timer, [this]() { recover(); }) and
           tick_timer.set_timeout(tick_interval) and
           ev_handler.register_event_handler(&tick_timer, [this]() { tick_handler(); });
}

void CanDevice::unregister_handlers() {
    ev_handler.unregister_event_handler(&tick_timer);
    ev_handler.unregister_event_handler(&recovery_timer);
    ev_handler.unregister_event_handler(can_bus.get());
}

void CanDevice::on_ready() {
    EVLOG_info << "CAN device " << device_name << " connected";
    link_up = true;
    link_handler(true);
}

void CanDevice::on_error(int error) {
    if (error == 0) {
        return;
    }
    if (link_up.value_or(true)) {
        EVLOG_error << "CAN device " << device_name << " not available, retrying";
        link_up = false;
        link_handler(false);
    } else {
        EVLOG_debug << "CAN device " << device_name << " still not available";
    }
}

void CanDevice::recover() {
    if (can_bus->on_error()) {
        can_bus->reset();
    }
}

bool CanDevice::tx_frame(uint32_t can_id, const std::vector<uint8_t>& payload) {
    if (payload.size() > CAN_MAX_DLEN) {
        EVLOG_error << "CAN payload too large (" << payload.size() << " bytes)";
        return false;
    }
    if (not can_bus or not link_up.value_or(false)) {
        return false;
    }
    if (can_bus->tx_queue_depth() > 0) {
        EVLOG_debug << "CAN device " << device_name << ": previous frame not sent yet, dropping frame";
        return false;
    }

    io::can::can_dataset data;
    data.set_can_id_with_flags(can_id);
    data.payload = payload;
    return can_bus->tx(data);
}

} // namespace can_device
