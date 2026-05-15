// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#pragma once
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/tls/tls.hpp>

#include <cstdint>
#include <vector>

namespace everest::lib::io::tls {

/**
 * @brief TLS server-side connection policy for fd_event_client.
 *
 * Wraps a tls::Server::ConnectionPtr and exposes the fd_event_client
 * ClientPolicy interface: handshake stepping, non-blocking tx/rx, and
 * fd accessors. The caller drives the event loop and calls handshake_step()
 * until handshake_complete() is true, then uses tx()/rx() for data transfer.
 */
class tls_server_socket {
public:
    /** @brief Payload type for tx/rx operations. */
    using PayloadT = std::vector<std::uint8_t>;

    /** @brief Default-constructs an unconnected socket. */
    tls_server_socket() = default;

    /**
     * @brief Construct from an accepted TLS connection.
     * @param conn Ownership of an accepted ServerConnection from
     *             tls::Server::wrap_accepted_fd().
     */
    explicit tls_server_socket(::tls::Server::ConnectionPtr conn);

    tls_server_socket(tls_server_socket const&) = delete;
    tls_server_socket(tls_server_socket&&) = default;
    tls_server_socket& operator=(tls_server_socket const&) = delete;
    tls_server_socket& operator=(tls_server_socket&&) = default;
    ~tls_server_socket() = default;

    /**
     * @brief Step the TLS handshake one iteration.
     *
     * Call repeatedly (re-arming the fd between calls) until either
     * handshake_complete() returns true or this returns false.
     *
     * @return true while progress is being made; false on permanent failure.
     *         When handshake_complete() becomes true the handshake finished
     *         successfully and no further calls are needed.
     */
    bool handshake_step();

    /**
     * @brief Query whether the TLS handshake has completed successfully.
     * @return true once accept() returned success.
     */
    bool handshake_complete() const;

    /**
     * @brief Send bytes over the TLS connection.
     *
     * On a partial write the sent prefix is erased from payload and false is
     * returned; the caller should re-arm and retry. Returns true only when
     * payload has been fully consumed.
     *
     * @param payload Bytes to send. Modified in place on partial writes.
     * @return true on complete send, false on partial send or want_*.
     */
    bool tx(PayloadT& payload);

    /**
     * @brief Receive bytes from the TLS connection.
     *
     * Replaces buffer contents with newly received data (fd_event_client
     * Policy contract). Returns false on want_* (no data yet).
     *
     * @param buffer Output buffer; cleared and overwritten on success.
     * @return true when data was received, false on want_* or error.
     */
    bool rx(PayloadT& buffer);

    /**
     * @brief Get the underlying socket file descriptor.
     * @return fd, or -1 if no connection is held.
     */
    int get_fd() const;

    /**
     * @brief Get the last recorded error code.
     * @return errno-style error value; 0 if no error.
     */
    int get_error() const;

    /**
     * @brief Query whether a live connection is held.
     * @return true while the ConnectionPtr is non-null.
     */
    bool is_open() const;

    /**
     * @brief Shut down and release the TLS connection.
     *
     * Issues a TLS shutdown and resets the internal ConnectionPtr.
     */
    void close();

    /**
     * @brief Which fd event the policy currently needs the loop to monitor.
     *
     * libtls returns want_read or want_write whenever the OpenSSL state
     * machine needs more I/O before it can make progress. handshake_step(),
     * tx(), and rx() update this accordingly:
     *  - want_read  → desired_events() == poll_events::read
     *  - want_write → desired_events() == poll_events::write
     *
     * Callers driving the loop manually should arm the returned event on the
     * fd before invoking the next call. The default (and the steady-state
     * value once handshake completes and the queue is empty) is read.
     *
     * @return The event the connection needs to make progress.
     */
    event::poll_events desired_events() const;

private:
    ::tls::Server::ConnectionPtr m_conn;
    event::poll_events m_desired{event::poll_events::read};
    bool m_handshake_done{false};
    int m_last_error{0};
};

} // namespace everest::lib::io::tls
