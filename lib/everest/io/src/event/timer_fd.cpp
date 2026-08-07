// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include <ctime>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/timer_fd.hpp>
#include <stdexcept>
#include <sys/timerfd.h>
#include <unistd.h>
#include <utility>

namespace everest::lib::io::event {

timer_fd::timer_fd() : m_fd(::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK)) {
    if (m_fd == -1) {
        throw std::runtime_error("failed to create an timerfd");
    }
}

// Runs before m_fd is destroyed, so the recorded descriptor is still the one the handler holds.
timer_fd::~timer_fd() {
    unregister_recorded_events();
}

timer_fd::operator int() const {
    return get_raw_fd();
}

int timer_fd::get_raw_fd() const {
    return m_fd;
}

bool timer_fd::valid() const {
    return m_fd != unique_fd::NO_DESCRIPTOR_SENTINEL;
}

int timer_fd::read() {
    uint64_t buffer;
    return ::read(m_fd, &buffer, sizeof(buffer));
}

bool timer_fd::reset() {
    return set_timeout_ns(m_to_ns);
}

void timer_fd::set_single_shot(bool on) {
    m_single_shot = on;
}

bool timer_fd::disarm() {
    struct itimerspec timer {};
    return ::timerfd_settime(m_fd, 0, &timer, nullptr) == 0;
}

bool timer_fd::set_timeout_ms(long long to) {
    return set_timeout_ns(1000 * 1000 * to);
}

bool timer_fd::set_timeout_us(long long to) {
    return set_timeout_ns(1000 * to);
}

bool timer_fd::set_timeout_ns(long long to) {
    m_to_ns = to;
    struct itimerspec timer {};
    auto const sec = to / 1000000000LL;
    auto const nano = to % 1000000000LL;

    timer.it_value.tv_sec = sec;
    timer.it_value.tv_nsec = nano;
    if (m_single_shot) {
        timer.it_interval.tv_sec = 0;
        timer.it_interval.tv_nsec = 0;
    } else {
        timer.it_interval.tv_sec = sec;
        timer.it_interval.tv_nsec = nano;
    }

    return ::timerfd_settime(m_fd, 0, &timer, nullptr) == 0;
}

// Compares against the handler map rather than the record alone: a registration dropped by
// descriptor leaves a record naming a live handler, and that must not block a new one.
bool timer_fd::has_recorded_registration() const {
    auto const live = m_registered_handler.lock();
    return live and live->handler and live->handler->is_registered(m_registered_fd);
}

void timer_fd::record_registration(std::shared_ptr<handler_liveness> handler, int fd) {
    m_registered_handler = std::move(handler);
    m_registered_fd = fd;
}

bool timer_fd::unregister_recorded_events(std::shared_ptr<handler_liveness> const& handler) {
    if (m_registered_handler.lock() != handler) {
        return false;
    }
    return unregister_recorded_events();
}

bool timer_fd::unregister_recorded_events() {
    auto const live = m_registered_handler.lock();
    auto const fd = m_registered_fd;
    m_registered_handler.reset();
    m_registered_fd = -1;
    return live and live->handler and live->handler->remove_event_handler(fd);
}

} // namespace everest::lib::io::event
