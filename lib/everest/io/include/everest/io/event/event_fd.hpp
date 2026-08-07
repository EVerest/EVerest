// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include "unique_fd.hpp"
#include <everest/io/event/handler_liveness.hpp>

#include <cstdint>
#include <memory>
#include <optional>

namespace everest::lib::io::event {

/**
 * event_fd_base creates an <a href="https://man7.org/linux/man-pages/man2/eventfd.2.html">event</a>.
 * The lifetime of the event is bound to the lifetime of this object.
 */
class event_fd_base {
public:
    /**
     * @brief Constructor
     * @param[in] initval The initial value for the internal counter of the eventfd
     * @param[in] flags for the creation of the internal eventfd.
     */
    event_fd_base(unsigned int initval, int flags);

    /**
     * @brief Declared virtual to ensure proper cleanup via base class pointer,
     * but defaulted to use the compiler-generated implementation.
     */
    virtual ~event_fd_base() = default;

    // fd_event_handler installs a lambda capturing the registered object, so a moved object
    // leaves the handler reading the event at the old address.
    event_fd_base(const event_fd_base&) = delete;
    event_fd_base& operator=(const event_fd_base&) = delete;

    event_fd_base(event_fd_base&&) = delete;
    event_fd_base& operator=(event_fd_base&&) = delete;

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
     * @brief Check if an event is held by this object
     * @details Compares internally to \ref unique_fd::NO_DESCRIPTOR_SENTINEL
     * @return True if an event filedescriptor is held, false otherwise
     */
    bool valid() const;

    /**
     * @brief Read from the eventfd
     * @details This returns the value of the eventfd internal counter.
     * Return immediately is the counter is non-zero. Depending on wether the eventfd
     * is used as a semaphore or not, calling this function either decrements the internal counter
     * (semaphore) or resets it to zero.
     * @return The value of the event counter read from the eventfd.
     * If the event cannot be read, the optional is a 'nullopt'
     */
    std::optional<std::uint64_t> read();

    /**
     * @brief Write to the eventfd
     * @details Adds 'data' to the eventfds internal counter.
     * This call blocks if adding 'data' to the internal counter would exceed the maximum value.
     * A call to read() is necessary to unblock.
     * @param[in] data Payload of the event
     * @return True on success, false otherwise
     */
    bool write(std::uint64_t data);

    /**
     * @brief Add a single event with default payload '1' to the event queue
     * @details Calles \ref write(1) internally.
     * @return True on success, false otherwise
     */
    bool notify();

private:
    unique_fd m_fd;
};

/**
 * event_fd creates a blocking eventfd with initial value '0'
 * The lifetime of the event is bound to the lifetime of this object.
 *
 * The registration record lives here rather than in \ref event_fd_base because
 * \ref fd_event_handler registers an \p event_fd, never an \p event_fd_base, so a record on the
 * base would be state \ref semaphore_fd can never use.
 */
class event_fd : public event_fd_base {
public:
    event_fd();

    /**
     * @brief Drops a registration recorded by \ref fd_event_handler
     * @details Runs before ~event_fd_base closes the descriptor, so the recorded descriptor is
     * still the one the handler holds. The record is a weak reference, so this touches nothing
     * once the handler is gone.
     */
    ~event_fd() override;

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

    std::weak_ptr<handler_liveness> m_registered_handler;
    int m_registered_fd{-1};
};

/**
 * event_fd creates a blocking eventfd as semaphore with initial value '0'
 * The lifetime of the event is bound to the lifetime of this object.
 */
class semaphore_fd : public event_fd_base {
public:
    semaphore_fd();
};

} // namespace everest::lib::io::event
