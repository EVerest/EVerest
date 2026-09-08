// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/socket/socket.hpp>
#include <everest/io/uds/uds_seqpacket_socket.hpp>

#include <cerrno>
#include <chrono>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <utility>

namespace everest::lib::io::uds {

bool uds_seqpacket_socket_base::tx(uds_payload const& payload) {
    return tx_impl(payload);
}

bool uds_seqpacket_socket_base::rx(uds_payload& payload) {
    if (not rx_impl(payload)) {
        return false;
    }
    if (payload.size() == 0 and not payload.has_fds()) {
        // recvmsg() answered zero bytes and no descriptor: an empty message, or the peer closed.
        // Only a closed peer raises POLLHUP; the next queued record cannot tell the two apart, it
        // may be empty itself.
        struct pollfd probe {};
        probe.fd = get_fd();
        probe.events = POLLIN;
        if (::poll(&probe, 1, 0) > 0 and (probe.revents & POLLHUP)) {
            record_io_error(ECONNRESET);
            return false;
        }
    }
    return true;
}

std::optional<uds_credentials> uds_seqpacket_socket_base::peer_credentials() const {
    if (not is_open()) {
        return std::nullopt;
    }
    return socket::get_peer_credentials(get_fd());
}

event::unique_fd uds_seqpacket_socket_base::peer_pidfd() const {
    if (not is_open()) {
        return {};
    }
    return socket::get_peer_pidfd(get_fd());
}

/////////////////////////////////////////////////

bool uds_seqpacket_client_socket::open(std::string const& remote, bool remote_abstract) {
    m_remote = remote;
    m_remote_abstract = remote_abstract;
    int error = 0;
    try {
        auto socket = socket::open_uds_seqpacket_client_socket(remote, remote_abstract);
        adopt(std::move(socket));
        // SO_ERROR is read-and-clear. The pending error is read once and kept, so a
        // false return still carries the reason instead of a value already consumed.
        error = get_error();
        if (error == 0) {
            return true;
        }
    } catch (socket::socket_error const& e) {
        error = e.error();
    } catch (...) {
    }
    record_connect_failure(error);
    return false;
}

bool uds_seqpacket_client_socket::setup(std::string const& remote, bool remote_abstract) {
    m_remote = remote;
    m_remote_abstract = remote_abstract;
    discard();
    return true;
}

void uds_seqpacket_client_socket::connect(std::function<void(bool, int)> const& setup_cb) {
    int error = 0;
    try {
        auto socket = socket::open_uds_seqpacket_client_socket(m_remote, m_remote_abstract);
        const auto fd = static_cast<int>(socket);
        adopt(std::move(socket));
        setup_cb(true, fd);
        return;
    } catch (socket::socket_error const& e) {
        error = e.error();
    } catch (...) {
    }
    record_connect_failure(error);
    std::this_thread::sleep_for(std::chrono::milliseconds(socket::reconnect_delay_ms));
    setup_cb(false, -1);
}

/////////////////////////////////////////////////

bool uds_seqpacket_peer_socket::open(shared_fd accepted) {
    if (not accepted or not accepted->is_fd()) {
        // Nothing to take over: never accepted, or already taken by the policy before a reset.
        record_connect_failure(ENOTCONN);
        return false;
    }
    try {
        // The listener accepts non blocking, but any shared_fd may be handed in here.
        socket::set_non_blocking(*accepted);
    } catch (...) {
        record_connect_failure(EBADF);
        return false;
    }
    adopt(std::move(*accepted));
    const int error = get_error();
    if (error != 0) {
        record_connect_failure(error);
        return false;
    }
    return true;
}

} // namespace everest::lib::io::uds
