// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <everest/io/event/registration_record.hpp>

namespace everest::lib::io::event {

class fd_event_handler;

/**
 * @brief Possible outcomes of syncing
 */
enum class sync_status {
    /** Sync was successful */
    ok,
    /** Sync timed out */
    timeout,
    /** Sync was not successful*/
    error
};

/**
 * Interface for classes implementing syncing (E/POLLIN only) for fd_event_handler
 */
class fd_event_sync_interface {
public:
    fd_event_sync_interface() = default;

    /**
     * @brief Drops a registration still recorded by \ref register_events
     */
    virtual ~fd_event_sync_interface();

    // register_events installs a lambda capturing this, so a moved object leaves the handler
    // calling sync() on the old address, independent of any registration record.
    fd_event_sync_interface(fd_event_sync_interface const&) = delete;
    fd_event_sync_interface& operator=(fd_event_sync_interface const&) = delete;
    fd_event_sync_interface(fd_event_sync_interface&&) = delete;
    fd_event_sync_interface& operator=(fd_event_sync_interface&&) = delete;

    /**
     * @brief Access to the internal event handler
     * @details Call \ref sync on read (E/POLLIN).
     * Override if an additional layer of event handler is necessary.
     * @return The file descriptor of the internal event handler
     */
    virtual int get_poll_fd() = 0;

    /**
     * @brief Sync internal event handler
     * @details Blocks until an event occurs.
     * @return Result of sync operation
     */
    virtual everest::lib::io::event::sync_status sync() = 0;

    /**
     * @brief Register with an existing event handler
     * @details Registers \ref get_poll_fd for read (E/POLLIN) and calls \ref sync
     * on notification. Handler and descriptor are recorded, so the registration can be removed
     * without asking this object again. Only one registration is recorded at a time.
     * @note \ref register_events and \ref unregister_events are not virtual by design. Registration
     * installs a callback bound to this object, so an override that skipped the recording would
     * leave the destructor unable to remove it and the handler calling \ref sync on a destroyed
     * object, a use after free rather than a leaked registration. Customize \ref get_poll_fd and
     * \ref sync instead, or implement \ref fd_event_register_interface for a registration of a
     * different shape, such as several descriptors or write events.
     * @param[in] handler The event handler to register with
     * @return true on success, false otherwise
     */
    bool register_events(fd_event_handler& handler);

    /**
     * @brief Unregister from an existing event handler
     * @param[in] handler The event handler passed to \ref register_events
     * @return true if a registration was removed, false otherwise
     */
    bool unregister_events(fd_event_handler& handler);

protected:
    /**
     * @brief Remove the recorded registration
     * @details Uses the recorded descriptor instead of \ref get_poll_fd, so this stays callable
     * while the object is being destroyed. Implementors call it in their destructor to leave the
     * handler before their own state is gone. The record is a weak reference, so this reports false
     * and touches nothing once the handler itself is gone.
     * @return true if a registration was removed, false otherwise
     */
    bool unregister_recorded_events();

private:
    registration_record m_record;
};

} // namespace everest::lib::io::event
