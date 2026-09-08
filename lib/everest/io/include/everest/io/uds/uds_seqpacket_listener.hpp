// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <everest/io/event/fd_event_sync_interface.hpp>
#include <everest/io/event/unique_fd.hpp>
#include <everest/io/uds/uds_seqpacket_peer.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <sys/types.h>

namespace everest::lib::io::uds {

/**
 * @brief Listening SOCK_SEQPACKET socket.
 * @details Register it with an \ref event::fd_event_handler. Every accepted connection is handed
 * to the accept callback as a \ref uds_seqpacket_peer; the callback takes ownership and registers
 * the peer with a handler. The listener itself never reads or writes messages. A bound filesystem
 * name is removed on destruction.
 */
class uds_seqpacket_listener : public event::fd_event_sync_interface {
public:
    /**
     * @var accept_cb
     * @brief Accept callback. Called from \ref sync with the new connection
     */
    using accept_cb = std::function<void(std::unique_ptr<uds_seqpacket_peer> peer)>;

    /**
     * @var error_cb
     * @brief Error callback for a failed accept. Never called with code 0
     */
    using error_cb = std::function<void(int error, std::string const& msg)>;

    /**
     * @brief Bind and listen.
     * @details A stale socket file at \p name is removed. A live socket there fails with
     * EADDRINUSE, a file that is not a socket with EEXIST.
     * @param[in] name Path or abstract name to listen on
     * @param[in] is_abstract True for the abstract namespace
     * @param[in] mode Permissions of the socket file, set between bind and listen. Connecting
     *            needs write permission. Only with a path; with an abstract name EINVAL is thrown
     * @throws socket::socket_error carrying the errno on failure
     */
    explicit uds_seqpacket_listener(std::string const& name, bool is_abstract = true,
                                    std::optional<mode_t> mode = std::nullopt);
    uds_seqpacket_listener(uds_seqpacket_listener const&) = delete;
    uds_seqpacket_listener(uds_seqpacket_listener&&) = delete;
    uds_seqpacket_listener& operator=(uds_seqpacket_listener const&) = delete;
    uds_seqpacket_listener& operator=(uds_seqpacket_listener&&) = delete;
    ~uds_seqpacket_listener() override;

    /**
     * @brief Set the accept callback. Without one, accepted connections are closed at once.
     */
    void set_accept_callback(accept_cb cb);

    /**
     * @brief Set the error callback. Descriptor exhaustion is reported as EMFILE or ENFILE and
     * costs the queued connection.
     */
    void set_error_handler(error_cb cb);

    /**
     * @brief The listening descriptor.
     */
    int get_poll_fd() override;

    /**
     * @brief Accept one queued connection and hand it to the accept callback.
     * @return ok if a connection was accepted or none was queued, error otherwise
     */
    event::sync_status sync() override;

private:
    void shed_queued_connection(int accept_errno);
    void report_error(int code, std::string const& what);

    event::unique_fd m_listen_fd;
    event::unique_fd m_reserve_fd;
    std::string m_bound_path;
    accept_cb m_cb;
    error_cb m_error;
};

} // namespace everest::lib::io::uds
