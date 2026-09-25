// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/timer_fd.hpp>
#include <everest/io/event/unique_fd.hpp>

namespace telemetry_router {

/// \brief Reads datagrams from a socket on its own thread and hands them to callbacks on that thread
class DatagramReceiver {
public:
    using DatagramHandler = std::function<void(const std::uint8_t* data, std::size_t size)>;
    using Handler = std::function<void()>;

    /// \param timer_interval period of \p on_timer; zero disables the timer
    DatagramReceiver(everest::lib::io::event::unique_fd socket, DatagramHandler on_datagram, Handler on_truncated,
                     std::chrono::milliseconds timer_interval, Handler on_timer);
    ~DatagramReceiver();

    DatagramReceiver(const DatagramReceiver&) = delete;
    DatagramReceiver& operator=(const DatagramReceiver&) = delete;

    void start();
    void stop();

private:
    void drain();

    everest::lib::io::event::unique_fd m_socket;
    DatagramHandler m_on_datagram;
    Handler m_on_truncated;
    std::chrono::milliseconds m_timer_interval;
    Handler m_on_timer;

    everest::lib::io::event::fd_event_handler m_events;
    everest::lib::io::event::event_fd m_stop_event;
    everest::lib::io::event::timer_fd m_timer;
    std::atomic_bool m_online{false};
    std::thread m_thread;
    std::vector<std::vector<std::uint8_t>> m_buffers;
};

} // namespace telemetry_router
