// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <everest/io/event/handler_liveness.hpp>

#include <memory>

namespace everest::lib::io::event {

/**
 * @brief A registration made with an \ref fd_event_handler, remembered by the registrant
 * @details Holds the handler's \ref handler_liveness block and the registered descriptor, so the
 * registration can be removed without asking the registrant again and without touching a handler
 * that no longer exists. \ref drop mutates the recording handler's map, so the owner must be
 * destroyed on that handler's thread (see \ref fd_event_handler).
 */
class registration_record {
public:
    registration_record() = default;

    // The handler map holds a callback bound to the recording object, so a second record of the
    // same registration must not exist: a copy would drop a registration its twin still counts on.
    registration_record(registration_record const&) = delete;
    registration_record& operator=(registration_record const&) = delete;
    registration_record(registration_record&&) = delete;
    registration_record& operator=(registration_record&&) = delete;

    /**
     * @brief True while the recorded registration still lives in the recording handler's map
     * @details Asks the map rather than the record alone: a registration dropped by raw
     * descriptor leaves a record naming a live handler, and that must not block a new one.
     */
    bool active() const;

    /**
     * @brief Remember a registration made with \p handler for \p fd
     * @param[in] handler The liveness block of the handler the registration was made with
     * @param[in] fd The registered file descriptor
     */
    void record(std::shared_ptr<handler_liveness> handler, int fd);

    /**
     * @brief Forget the record, removing the registration while the handler is alive
     * @details The record is cleared before calling out, so re-entry cannot see a half-dropped
     * state. Touches nothing once the handler is gone.
     * @return true if a registration was removed, false otherwise
     */
    bool drop();

    /**
     * @brief \ref drop, but only if the record names \p handler
     * @param[in] handler The liveness block to compare against
     * @return true if a registration was removed, false otherwise
     */
    bool drop_if(std::shared_ptr<handler_liveness> const& handler);

private:
    std::weak_ptr<handler_liveness> m_handler;
    int m_fd{-1};
};

} // namespace everest::lib::io::event
