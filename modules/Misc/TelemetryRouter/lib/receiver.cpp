// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include "receiver.hpp"

#include <array>
#include <cerrno>

#include <sys/socket.h>

#include <utils/telemetry/wire.hpp>

namespace telemetry_router {

namespace {
constexpr std::size_t BATCH_SIZE = 16;
} // namespace

DatagramReceiver::DatagramReceiver(everest::lib::io::event::unique_fd socket, DatagramHandler on_datagram,
                                   Handler on_truncated, std::chrono::milliseconds timer_interval, Handler on_timer) :
    m_socket(std::move(socket)),
    m_on_datagram(std::move(on_datagram)),
    m_on_truncated(std::move(on_truncated)),
    m_timer_interval(timer_interval),
    m_on_timer(std::move(on_timer)),
    m_buffers(BATCH_SIZE, std::vector<std::uint8_t>(everest::telemetry::wire::MAX_DATAGRAM_SIZE)) {
}

DatagramReceiver::~DatagramReceiver() {
    stop();
}

void DatagramReceiver::start() {
    if (m_thread.joinable()) {
        return;
    }
    m_events.register_event_handler(
        static_cast<int>(m_socket), [this](const auto&) { drain(); }, everest::lib::io::event::poll_events::read);
    m_events.register_event_handler(&m_stop_event, [this](const auto&) { m_online = false; });
    if (m_timer_interval.count() > 0 and m_on_timer) {
        m_timer.set_timeout(m_timer_interval);
        m_events.register_event_handler(&m_timer, [this](const auto&) { m_on_timer(); });
    }
    m_online = true;
    m_thread = std::thread([this] { m_events.run(m_online); });
}

void DatagramReceiver::stop() {
    if (not m_thread.joinable()) {
        return;
    }
    m_stop_event.notify();
    m_thread.join();
}

void DatagramReceiver::drain() {
    std::array<mmsghdr, BATCH_SIZE> messages{};
    std::array<iovec, BATCH_SIZE> vectors{};
    while (true) {
        for (std::size_t i = 0; i < BATCH_SIZE; ++i) {
            vectors[i] = {m_buffers[i].data(), m_buffers[i].size()};
            messages[i] = {};
            messages[i].msg_hdr.msg_iov = &vectors[i];
            messages[i].msg_hdr.msg_iovlen = 1;
        }
        const auto received =
            ::recvmmsg(static_cast<int>(m_socket), messages.data(), BATCH_SIZE, MSG_DONTWAIT, nullptr);
        if (received <= 0) {
            return;
        }
        for (int i = 0; i < received; ++i) {
            if ((messages[i].msg_hdr.msg_flags & MSG_TRUNC) != 0) {
                m_on_truncated();
                continue;
            }
            m_on_datagram(m_buffers[i].data(), messages[i].msg_len);
        }
        if (static_cast<std::size_t>(received) < BATCH_SIZE) {
            return;
        }
    }
}

} // namespace telemetry_router
