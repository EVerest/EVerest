// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <everest/io/can/socket_can.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/timer_fd.hpp>

namespace can_device {

// A SocketCAN interface driven on its own event thread. The interface is reopened every second while it is on error;
// frames, ticks and link changes are delivered to the subclass on that thread.
class CanDevice {
public:
    explicit CanDevice(std::chrono::milliseconds tick_interval);
    virtual ~CanDevice();
    CanDevice(const CanDevice&) = delete;
    CanDevice& operator=(const CanDevice&) = delete;
    CanDevice(CanDevice&&) = delete;
    CanDevice& operator=(CanDevice&&) = delete;

    // A missing interface is not an error: it is reopened until it appears. Fails on an invalid name, when the
    // device is already open, or when an event registration fails.
    bool open_device(const std::string& dev);
    // Subclasses call this from their destructor, before the handlers it invokes are destroyed.
    void close_device();

protected:
    virtual void rx_handler(uint32_t can_id, const std::vector<uint8_t>& payload) = 0;
    // Every tick interval while the device is open, also while the link is down.
    virtual void tick_handler() = 0;
    // Whenever the interface is (re)opened or lost.
    virtual void link_handler(bool up) = 0;
    // Event thread only, so from the handlers above: socket_can is not thread safe. Returns false when the frame was
    // dropped, which happens while the link is down or while the previous frame has not left the socket.
    bool tx_frame(uint32_t can_id, const std::vector<uint8_t>& payload);

private:
    void on_ready();
    void on_error(int error);
    void recover();
    bool register_handlers();
    void unregister_handlers();

    const std::chrono::milliseconds tick_interval;
    std::string device_name;
    std::unique_ptr<everest::lib::io::can::socket_can> can_bus;
    everest::lib::io::event::fd_event_handler ev_handler;
    everest::lib::io::event::timer_fd recovery_timer;
    everest::lib::io::event::timer_fd tick_timer;
    std::optional<bool> link_up;
    std::atomic_bool ev_thread_online{false};
    std::thread ev_thread;
};

} // namespace can_device
