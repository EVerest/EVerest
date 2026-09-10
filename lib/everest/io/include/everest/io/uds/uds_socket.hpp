// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <array>
#include <cstdint>
#include <everest/io/event/unique_fd.hpp>
#include <everest/io/uds/uds_payload.hpp>
#include <functional>
#include <optional>
#include <string>
#include <sys/types.h>

namespace everest::lib::io::uds {

/**
 * @struct uds_info
 * @brief Address of a unix domain socket.
 */
struct uds_info {
    /** Filesystem path or abstract name */
    std::string path;
    /** True for the abstract namespace: no file, no permissions, gone with the last socket */
    bool is_abstract{true};
};

/**
 * @brief Common part of the unix domain socket policies: descriptor lifetime, sending and
 * receiving \ref uds_payload, error reporting.
 * @details Not usable on its own. The derived classes are the \p ClientPolicy implementations for
 * \ref event::fd_event_client.
 */
class uds_socket_base {
public:
    uds_socket_base() = default;
    /**
     * @brief Closes the socket and removes the filesystem name it bound, if any.
     */
    virtual ~uds_socket_base();

    /**
     * @brief True if a socket is owned.
     */
    bool is_open() const;

    /**
     * @brief Close the socket. A bound filesystem name is removed.
     */
    void close();

    /**
     * @brief File descriptor of the socket, -1 if none.
     */
    int get_fd() const;

    /**
     * @brief Current error of the socket, 0 if none.
     * @details Reports, in this order: the errno of the last failed send or receive (EAGAIN and
     * EINTR excluded), the errno of the last failed open or connect, SO_ERROR. AF_UNIX reports a
     * vanished peer synchronously on the send, so the first is what makes an event client reset.
     */
    int get_error() const;

protected:
    /**
     * @brief Send \p payload to the connected peer.
     * @details The errno of a failed send is recorded for \ref get_error. A payload exceeding
     * \ref uds_payload::max_size or \ref uds_payload::max_fds can never be delivered and is
     * dropped, reported as sent.
     * @return True if sent or dropped as undeliverable, false otherwise
     */
    bool tx_impl(uds_payload const& payload);

    /**
     * @brief Send \p payload to \p destination.
     * @details As \ref tx_impl(uds_payload const&). Also dropped and reported as sent: an empty
     * \p destination, and a peer that has gone or is unreachable (ECONNREFUSED, ENOENT, ENOTCONN,
     * EPERM, EACCES).
     * @return True if sent or dropped as undeliverable, false otherwise
     */
    bool tx_impl(uds_payload const& payload, uds_info const& destination);

    /**
     * @brief Receive one message into \p payload, replacing its bytes, descriptors and credentials.
     * @details A message exceeding \ref uds_payload::max_size or \ref uds_payload::max_fds is
     * dropped whole, its descriptors closed. Received descriptors are close-on-exec.
     * @return The sender, std::nullopt if nothing was received. An unnamed sender has an empty path
     */
    std::optional<uds_info> rx_impl(uds_payload& payload);

    /**
     * @brief Take ownership of \p fd.
     * @param[in] fd The socket
     * @param[in] bound_path Filesystem name \p fd is bound to, removed on \ref close. Empty if none
     */
    void adopt(event::unique_fd&& fd, std::string bound_path = {});

    /**
     * @brief Drop the socket and remember \p error as the reason there is none.
     */
    void record_connect_failure(int error);

    /**
     * @brief Drop the socket, remove its bound filesystem name and clear all recorded errors.
     */
    void discard();

    /**
     * @brief Remove the bound filesystem name, if any, while the socket is still open.
     */
    void release_bound_path();

    /**
     * @brief Record the errno of a failed send or receive. EAGAIN, EWOULDBLOCK and EINTR are
     * ignored.
     */
    void record_io_error(int error);

private:
    event::unique_fd m_owned_uds_fd;
    std::string m_bound_path;
    int m_connect_error{0};
    int m_io_error{0};
    std::array<uint8_t, uds_payload::max_size> m_rx_buffer;
};

/**
 * @brief Datagram client policy for \ref event::fd_event_client, see \ref uds_client.
 * @details Connected to one server. Always bound to a name of its own, so the server can reply.
 */
class uds_client_socket : public uds_socket_base {
public:
    /** Payload type */
    using PayloadT = uds_payload;

    uds_client_socket() = default;
    ~uds_client_socket() = default;

    /**
     * @brief Open and connect. Non blocking.
     * @param[in] remote Server path or abstract name
     * @param[in] remote_abstract True if \p remote is abstract
     * @param[in] local Own name to bind. Empty: the kernel assigns a unique abstract name (autobind)
     * @param[in] local_abstract True if \p local is abstract. A filesystem \p local is removed on
     *            \ref close
     * @param[in] with_peer_credentials True to receive the sender's identity in
     *            \ref uds_payload::credentials with every reply
     * @return True on success, false otherwise. See \ref get_error
     */
    bool open(std::string const& remote, bool remote_abstract, std::string const& local = "",
              bool local_abstract = true, bool with_peer_credentials = false);

    /**
     * @brief Store the parameters for \ref connect. Same parameters as \ref open.
     * @return Always true
     */
    bool setup(std::string const& remote, bool remote_abstract, std::string const& local = "",
               bool local_abstract = true, bool with_peer_credentials = false);

    /**
     * @brief Connect with the parameters of \ref setup and report through \p setup_cb.
     * @details Sleeps \ref socket::reconnect_delay_ms before reporting a failure.
     */
    void connect(std::function<void(bool, int)> const& setup_cb);

    /**
     * @brief Send \p payload to the server, see \ref tx_impl(uds_payload const&).
     */
    bool tx(uds_payload const& payload);

    /**
     * @brief Receive one message, see \ref rx_impl.
     */
    bool rx(uds_payload& payload);

private:
    std::string m_remote;
    bool m_remote_abstract{true};
    std::string m_local;
    bool m_local_abstract{true};
    bool m_with_peer_credentials{false};
};

/////////////////////////////////////////////////

/**
 * @brief Datagram server policy for \ref event::fd_event_client, see \ref uds_server.
 * @details One socket serves any number of clients. \ref tx answers the sender of the last
 * message received.
 */
class uds_server_socket : public uds_socket_base {
public:
    /** Payload type */
    using PayloadT = uds_payload;

    uds_server_socket() = default;
    ~uds_server_socket() = default;

    /**
     * @brief Bind. Non blocking.
     * @details A stale socket file at \p path is removed. A live socket there fails with
     * EADDRINUSE, a file that is not a socket with EEXIST. The file is removed on \ref close.
     * @param[in] path Path or abstract name to bind
     * @param[in] is_abstract True for the abstract namespace
     * @param[in] with_peer_credentials True to receive the sender's identity in
     *            \ref uds_payload::credentials with every message
     * @param[in] mode Permissions of the socket file, set after bind. Connecting needs write
     *            permission. Only with a path; with an abstract name the open fails with EINVAL
     * @return True on success, false otherwise. See \ref get_error
     */
    bool open(std::string const& path, bool is_abstract = true, bool with_peer_credentials = false,
              std::optional<mode_t> mode = std::nullopt);

    /**
     * @brief Send \p payload to the sender of the last received message.
     * @details Dropped and reported as sent if no message arrived yet, if the last sender had no
     * name, or if it has gone. See \ref tx_impl(uds_payload const&, uds_info const&).
     */
    bool tx(uds_payload const& payload);

    /**
     * @brief Receive one message and remember its sender for \ref tx, see \ref rx_impl.
     */
    bool rx(uds_payload& payload);

private:
    std::optional<uds_info> m_last_source;
};

} // namespace everest::lib::io::uds
