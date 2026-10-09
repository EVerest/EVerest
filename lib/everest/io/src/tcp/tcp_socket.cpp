// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <cerrno>
#include <chrono>
#include <everest/io/socket/socket.hpp>
#include <everest/io/tcp/tcp_socket.hpp>
#include <netdb.h>
#include <thread>

namespace everest::lib::io::tcp {

void tcp_socket::adopt(event::unique_fd&& fd) {
    m_fd = std::move(fd);
    m_connect_error = 0;
    m_error.clear();
}

void tcp_socket::record_connect_failure(int error) {
    m_fd.close();
    m_connect_error = error;
}

void tcp_socket::discard() {
    m_fd.close();
    m_connect_error = 0;
    m_error.clear();
}

bool tcp_socket::open(std::string const& remote, uint16_t port, std::string const& device) {
    m_error.clear();
    m_remote = remote;
    m_port = port;
    m_timeout_ms = 1000;
    m_device = device;
    int error = 0;
    try {
        auto socket = socket::open_tcp_socket_with_timeout(remote, port, m_timeout_ms, m_device);
        socket::set_non_blocking(socket);
        adopt(std::move(socket));
        // SO_ERROR is read-and-clear. The pending error is read once and kept, so a
        // false return still carries the reason instead of a value already consumed.
        error = socket::get_pending_error(m_fd);
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

bool tcp_socket::setup(std::string const& remote, uint16_t port, int timeout_ms, std::string const& device) {
    m_remote = remote;
    m_port = port;
    m_timeout_ms = timeout_ms;
    m_device = device;
    discard();
    return true;
}

void tcp_socket::connect(std::function<void(bool, int)> const& setup_cb) {
    int error = 0;
    try {
        auto socket = socket::open_tcp_socket_with_timeout(m_remote, m_port, m_timeout_ms, m_device);
        socket::set_non_blocking(socket);
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

bool tcp_socket::tx(PayloadT& payload) {
    if (not is_open()) {
        return false;
    }

    // A peer that reset is reported as EPIPE; without the flag the kernel raises SIGPIPE first,
    // which ends a process that does not ignore it.
    auto status = ::send(m_fd, payload.data(), payload.size(), MSG_NOSIGNAL);
    m_error.note(status == -1 ? errno : 0);
    if (status == -1) {
        return false;
    }
    if (status < static_cast<ssize_t>(payload.size())) {
        // Keep the unsent bytes in place and return false: the block stays in the buffer.
        payload.erase(payload.begin(), payload.begin() + status);
        return false;
    }
    return true;
}

bool tcp_socket::rx(PayloadT& buffer) {
    if (not is_open()) {
        return false;
    }
    buffer.resize(default_buffer_size);
    auto status = ::recv(m_fd, buffer.data(), buffer.size(), 0);
    m_error.note(status == -1 ? errno : 0);
    if (status <= 0) { // -1 is an error, 0 is a connection closed by the peer
        return false;
    }
    buffer.resize(status);
    return true;
}

int tcp_socket::get_fd() const {
    return m_fd;
}

int tcp_socket::get_error() const {
    if (not is_open()) {
        // Zero means "nothing recorded", not "healthy": falling through to the probe of
        // an unassigned descriptor yields EBADF, and that nonzero value is what makes
        // the client reset and reconnect. Reporting zero here would read as healthy.
        return m_connect_error != 0 ? m_connect_error : socket::get_pending_error(m_fd);
    }
    // The pending and the recorded error come before the liveness peek: a peek on a socket the
    // kernel has aborted returns and clears that error (ETIMEDOUT for TCP_USER_TIMEOUT), and
    // the connection would then be reported as reset by a peer that reset nothing.
    auto const recorded = m_error.report(m_fd);
    if (recorded != 0) {
        return recorded;
    }
    char byte = 0;
    auto const peeked = ::recv(m_fd, &byte, sizeof(byte), MSG_PEEK | MSG_DONTWAIT);
    if (peeked > 0) {
        return 0;
    }
    if (peeked == -1) {
        auto const error = errno;
        return (error == EAGAIN or error == EWOULDBLOCK or error == EINTR) ? 0 : error;
    }
    // The peer shut its side without an error: the library's code for a stream peer closing.
    return ECONNRESET;
}

bool tcp_socket::is_open() const {
    return m_fd.is_fd();
}

void tcp_socket::close() {
    discard();
}

int tcp_socket::release() {
    return m_fd.release();
}

bool tcp_socket::set_keep_alive(uint32_t count, uint32_t idle_s, uint32_t intval_s) {
    if (not is_open()) {
        return false;
    }

    try {
        socket::set_tcp_keepalive(m_fd, count, idle_s, intval_s);
        return true;
    } catch (...) {
    }
    return false;
}

bool tcp_socket::set_user_timeout(uint32_t to_ms) {
    if (not is_open()) {
        return false;
    }

    try {
        socket::set_tcp_user_timeout(m_fd, to_ms);
        return true;
    } catch (...) {
    }
    return false;
}

} // namespace everest::lib::io::tcp
