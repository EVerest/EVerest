// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once
#include <cstdint>
#include <everest/io/serial/serial.hpp>
#include <vector>

namespace everest::lib::io::serial {

/**
 * @struct pty_status
 * Simplified status of a <a href="https://man7.org/linux/man-pages/man7/pty.7.html">PTY</a>.
 * Values map to the values defined <a href="https://linux.die.net/man/3/tcgetattr">here</a>
 */
struct pty_status {
    /**
     * @brief Software flow control for the output
     */
    bool ixon{false};
    /**
     * @brief Software flow control for the input
     */
    bool ixoff{false};
    /**
     * @brief Two stopbits instead of one
     */
    bool cstopb{false};
    /**
     * @brief The Baud reate
     */
    unsigned int cbaud{0};
};

/**
 * pty_handler bundles basic <a href="https://man7.org/linux/man-pages/man7/pty.7.html">PTY</a>
 * related functionality.  This includes lifetime management, reading, writing and fundamental
 * error checking. <br>
 * Although this class can be used on its own, the main purpose is to implement the
 * \p ClientPolicy of \ref event::fd_event_client
 *
 * The master is non blocking: a write the slave has not made room for returns false from \ref tx
 * with the unsent bytes left in the payload, and the client retries on the next writable event.
 * Undrained data queues in the client's tx buffer up to
 * \ref event::generic_fd_event_client::max_buffered_tx_payloads, then tx() rejects. A consumer
 * that must not lose data throttles its source (tx_queue_depth(), set_tx_drained_action(),
 * pause_rx()); a blocking master would stall the whole event loop instead.
 */
class pty_handler {
public:
    /**
     * @var PayloadT
     * @brief Type of the payload for tX and RX operations
     */
    using PayloadT = std::vector<uint8_t>;

    /**
     * @var supports_tx_coalescing
     * @brief Byte stream, \ref tx leaves exactly the unsent bytes in the payload.
     */
    static constexpr bool supports_tx_coalescing{true};

    /**
     * @brief The class is default constructed
     */
    pty_handler() = default;
    ~pty_handler() = default;

    /**
     * @brief Write a dataset to the PTY
     * @details Implementation for \p ClientPolicy. A partial write removes the written bytes from
     * \p data, a would-block leaves it unchanged; both return false with \ref get_error zero.
     * @param[in] data Payload
     * @return True on success, false on failure, partial write or would-block.
     */
    bool tx(PayloadT& data);
    /**
     * @brief Read a dataset from the PTY
     * @details Implementation for \p ClientPolicy. Nothing pending returns false with \ref get_error
     * zero.
     * @param[in] data Payload
     * @return True on success, False otherwise.
     */
    bool rx(PayloadT& data);

    /**
     * @brief Open the PTY
     * @details Activates <a href="https://lists.gnu.org/archive/html/bug-readline/2011-01/msg00004.html">EXTPROC</a>
     * an <a href="https://man7.org/linux/man-pages/man2/TIOCPKT.2const.html">TIOCPKT</a>
     * via \ref make_pty_mode_aware; the master is made non blocking. <br>
     * Implementation for \p ClientPolicy
     * @return True on success, false otherwise.
     */
    bool open();

    /**
     * @brief Get the master file descriptor
     * @details Implementation for ClientPolicy
     * @return master file descriptor
     */
    int get_fd() const;

    /**
     * @brief Get the current error
     * @details Implementation for \p ClientPolicy. Set by \ref open, \ref tx and \ref rx only; a
     * would-block or partial write leaves it zero, \ref get_status never sets it.
     * @return The last errno. Zero if there is no error.
     */
    int get_error() const;

    /**
     * @brief Get the current status of the PTY
     * @details Read the status information as set on the slave
     * @return Simplified status
     */
    pty_status get_status();

    /**
     * @brief The path of the slave in the file system
     * @return Path
     */
    std::string get_slave_path() const;

private:
    pty m_dev;
    int error_id{0};
    static constexpr size_t buffer_size_limit = 1400;
};

} // namespace everest::lib::io::serial
