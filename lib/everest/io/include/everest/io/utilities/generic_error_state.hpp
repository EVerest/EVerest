// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <functional>
#include <string>

namespace everest::lib::io::utilities {

/**
 * @enum connection_state
 * @brief The connection state of a client.
 * @details Separates a client that has not completed a connection attempt yet from one whose
 * connection attempt failed. \ref generic_error_state::on_error covers both as "not up".
 */
enum class connection_state {
    /** No connection attempt has completed since the device was last opened */
    fresh,
    /** Connected and usable */
    connected,
    /** A connection attempt or an established connection failed */
    failed
};

/**
 * This class bundles some handling logic for <a href="https://man7.org/linux/man-pages/man3/errno.3.html">errno</a>.
 * <br> Its main purpose is the factor out functionality from \ref event::generic_fd_event_client_impl
 */
class generic_error_state {
public:
    /**
     * @var cb_error
     * @brief Prototype for an on_error handler callback. It receives the stored error code
     * and a description of it
     */
    using cb_error = std::function<void(int error, std::string const& msg)>;
    virtual ~generic_error_state() = default;

protected:
    /**
     * @brief Update the error state
     * @details Compares with internal error state and update it. A nonzero code moves the
     * connection state to \ref connection_state::failed, a zero code to
     * \ref connection_state::connected. Only \ref reset_connection_state returns to
     * \ref connection_state::fresh.
     *
     * A zero code arriving in any state other than \ref connection_state::connected is an
     * up-edge on the connection and arms \ref clear_error_pending. Because
     * \ref connection_state::fresh counts as an up-edge just like
     * \ref connection_state::failed, a connection that never reported a failure still signals
     * the one it establishes. Every other call clears \ref clear_error_pending: a nonzero code
     * because there is no up-edge to report, a zero code in \ref connection_state::connected
     * because the connection was already up.
     * @param[in] error_code The error state to be set
     * @return False if error status is set, True otherwise
     */
    bool set_error_status(int error_code);

    /**
     * @brief Check whether a connection up-edge is waiting to be reported.
     * @details \ref set_error_status rewrites this on every call: a zero code arriving in a state
     * other than \ref connection_state::connected arms it, every other combination clears it.
     * \ref clear_error_handler and \ref set_error_cleared clear it as well. Reporting an up-edge
     * exactly once per successful connect is gated on this.
     * @return True if an up-edge is pending, false otherwise
     */
    bool clear_error_pending() const;

    /**
     * @brief Check the error state
     * @details Covers \ref connection_state::fresh and \ref connection_state::failed alike.
     * @return True if not up, false otherwise
     */
    bool on_error() const;

    /**
     * @brief Check the connection state
     * @return \ref connection_state::fresh, \ref connection_state::connected or \ref connection_state::failed
     */
    connection_state current_connection_state() const;

    /**
     * @brief Return the connection state to \ref connection_state::fresh
     * @details To be called when the device is opened again, so a state left over from a previous
     * connection is not read as the state of the new one. Leaves the current error code untouched,
     * which is safe because the code is only meaningful in \ref connection_state::failed.
     */
    void reset_connection_state();

    /**
     * @brief Get the current error code
     * @details Only meaningful in \ref connection_state::failed. In
     * \ref connection_state::fresh the code left over from an earlier connection is still there.
     * @return Current error code
     */
    int current_error() const;

    /**
     * @brief Call the error handler with the stored error code, if registered.
     * @details The reported error is never 0. Clearing an error is signaled through
     *          \ref clear_error_handler instead.
     * @param[in] handler The handler to be called
     * @param[in] msg Description of the error. An empty string selects the description
     *            of the stored error code.
     */
    void call_error_handler(cb_error& handler, std::string const& msg = {}) const;

    /**
     * @brief Report a connection up-edge by calling the handler with errno=0 (success)
     * @details Code 0 means the connection is established, not only that a previously reported
     * error is gone. Disarms \ref clear_error_pending, so an up-edge is reported once per
     * successful connect.
     * @param[in] handler The handler to be called
     */
    void clear_error_handler(cb_error& handler);

    /**
     * @brief Disarm the pending connection up-edge without reporting it
     */
    void set_error_cleared();

private:
    connection_state m_connection_state{connection_state::fresh};
    bool m_clear_error_pending{false};
    int m_current_error{0};
};

} // namespace everest::lib::io::utilities
