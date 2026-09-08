// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <everest/io/uds/uds_payload.hpp>
#include <everest/io/uds/uds_socket.hpp>
#include <functional>
#include <optional>
#include <string>

namespace everest::lib::io::uds {

/**
 * @brief Connected end of a SOCK_SEQPACKET connection, either side.
 * @details Messages keep their boundaries like datagrams. Unlike datagrams there is a peer, and
 * the peer going away is reported through \ref get_error.
 */
class uds_seqpacket_socket_base : public uds_socket_base {
public:
    /** Payload type */
    using PayloadT = uds_payload;

    /**
     * @brief Send \p payload to the peer, see \ref tx_impl(uds_payload const&).
     * @details A peer that has gone is a failure (EPIPE), unlike for a datagram server.
     */
    bool tx(uds_payload const& payload);

    /**
     * @brief Receive one message, see \ref rx_impl.
     * @details A closed peer is reported as ECONNRESET. An empty message is delivered as such; an
     * empty message that was queued when the peer closed may be dropped.
     */
    bool rx(uds_payload& payload);

    /**
     * @brief Identity of the peer, recorded by the kernel at connect or accept time.
     * @details On an event client reach it through get_raw_handler().
     * @return The credentials, std::nullopt without a connection
     */
    std::optional<uds_credentials> peer_credentials() const;

    /**
     * @brief pidfd of the peer process (SO_PEERPIDFD, Linux 6.5 and later).
     * @return The pidfd, empty without a connection or without kernel support
     */
    event::unique_fd peer_pidfd() const;
};

/**
 * @brief Connecting side of a SOCK_SEQPACKET connection, see \ref uds_seqpacket_client.
 */
class uds_seqpacket_client_socket : public uds_seqpacket_socket_base {
public:
    uds_seqpacket_client_socket() = default;
    ~uds_seqpacket_client_socket() = default;

    /**
     * @brief Connect to a listener. Non blocking.
     * @param[in] remote Listener path or abstract name
     * @param[in] remote_abstract True if \p remote is abstract
     * @return True on success, false otherwise. See \ref get_error
     */
    bool open(std::string const& remote, bool remote_abstract = true);

    /**
     * @brief Store the parameters for \ref connect. Same parameters as \ref open.
     * @return Always true
     */
    bool setup(std::string const& remote, bool remote_abstract = true);

    /**
     * @brief Connect with the parameters of \ref setup and report through \p setup_cb.
     * @details Sleeps \ref socket::reconnect_delay_ms before reporting a failure.
     */
    void connect(std::function<void(bool, int)> const& setup_cb);

private:
    std::string m_remote;
    bool m_remote_abstract{true};
};

/**
 * @brief Accepted side of a SOCK_SEQPACKET connection, see \ref uds_seqpacket_peer.
 * @details Handed out by \ref uds_seqpacket_listener. Single use: a reset finds no connection and
 * fails with ENOTCONN. Drop the peer on error.
 */
class uds_seqpacket_peer_socket : public uds_seqpacket_socket_base {
public:
    uds_seqpacket_peer_socket() = default;
    ~uds_seqpacket_peer_socket() = default;

    /**
     * @brief Take over an accepted connection.
     * @details Moves the descriptor out of \p accepted, which stays empty. A second open with the
     * same handle fails with ENOTCONN.
     * @param[in] accepted The accepted connection. Made non blocking
     * @return True on success, false otherwise
     */
    bool open(shared_fd accepted);
};

} // namespace everest::lib::io::uds
