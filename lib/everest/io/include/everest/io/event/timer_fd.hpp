// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include "unique_fd.hpp"
#include <everest/io/event/handler_liveness.hpp>

#include <chrono>
#include <memory>

namespace everest::lib::io::event {

/**
 * timer_fd creates and configures a <a href="https://man7.org/linux/man-pages/man2/timerfd_create.2.html">timer</a>.
 * The lifetime of the timer is bound to the lifetime of this object.
 */
class timer_fd {
public:
    /**
     * @brief Constructor
     * @details After construction the timeout is undefined. It must be set manually.
     */
    timer_fd();

    /**
     * @brief Drops a registration recorded by \ref fd_event_handler
     * @details The record is a weak reference, so this touches nothing once the handler is gone.
     */
    ~timer_fd();

    // fd_event_handler installs a lambda capturing this, so a moved object leaves the handler
    // reading the timer at the old address.
    timer_fd(timer_fd const&) = delete;
    timer_fd& operator=(timer_fd const&) = delete;
    timer_fd(timer_fd&&) = delete;
    timer_fd& operator=(timer_fd&&) = delete;

    /**
     * @brief Explicit conversion to file descriptor
     * @return The internal file descriptor
     */
    explicit operator int() const;

    /**
     * @brief Access to internal file descriptor
     * @return The internal file descriptor
     */
    int get_raw_fd() const;

    /**
     * @brief Check if a timer is held by this object
     * @details Compares internally to \ref unique_fd::NO_DESCRIPTOR_SENTINEL
     * @return True if a timer is held, false otherwise
     */
    bool valid() const;

    /**
     * @brief Read from timer
     * @details This acknowledges, that the timer event has been handled. Poll on this object will
     * return immediately until this function has been called after a timeout event.
     * @return The value read from the timer
     */
    int read();

    /**
     * @brief Resets the timer
     * @details This starts a new timeout period for an already set running timer.
     * @return True on success, false otherwise
     */
    bool reset();

    /**
     * @brief Select one-shot or periodic mode for subsequent \ref set_timeout_ns calls.
     * @details One-shot: \c it_value is the delay, \c it_interval is zero. Periodic (default):
     * both \c it_value and \c it_interval use the configured timeout.
     */
    void set_single_shot(bool on);

    /**
     * @brief Stop the timer (no pending expiry until armed again).
     * @return True on success, false otherwise
     */
    bool disarm();

    /**
     * @name Configuring the notification timeout
     * This set of functions allows to set the time after which the timer file fire
     * @{
     */
    /**
     * @brief Milliseconds
     * @param[in] to timeout in milliseconds
     * @return True on success, false otherwise
     */
    bool set_timeout_ms(long long to);
    /**
     * @brief Microseconds
     * @param[in] to timeout in microseconds
     * @return True on success, false otherwise
     */
    bool set_timeout_us(long long to);
    /**
     * @brief Nanoseconds
     * @param[in] to timeout in nanoseconds
     * @return True on success, false otherwise
     */
    bool set_timeout_ns(long long to);

    /**
     * @brief <a href="https://en.cppreference.com/w/cpp/chrono/duration"> std::chrono::duration </a>
     * @param[in] timeout timeout in abitrary  units
     * @return True on success, false otherwise
     */
    template <class Rep, class Period> bool set_timeout(std::chrono::duration<Rep, Period> timeout) {
        auto interval = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout);
        return set_timeout_ns(interval.count());
    }
    /**
     * @}
     */

private:
    // Only the handler may write the record, so a registration is recorded exactly once and by
    // the side that made it.
    friend class fd_event_handler;

    /// True while a recorded registration is still in place with a live handler
    bool has_recorded_registration() const;

    /// Record the registration \p handler made for \p fd
    void record_registration(std::shared_ptr<handler_liveness> handler, int fd);

    /**
     * @brief Drop the record if it names \p handler
     * @return true if a registration was removed, false otherwise
     */
    bool unregister_recorded_events(std::shared_ptr<handler_liveness> const& handler);

    /**
     * @brief Drop the record, removing the registration while the handler is alive
     * @details Uses the recorded descriptor instead of \ref get_raw_fd, so this stays callable
     * while the object is being destroyed.
     * @return true if a registration was removed, false otherwise
     */
    bool unregister_recorded_events();

    unique_fd m_fd;
    long long m_to_ns{0};
    bool m_single_shot{false};
    std::weak_ptr<handler_liveness> m_registered_handler;
    int m_registered_fd{-1};
};

} // namespace everest::lib::io::event
