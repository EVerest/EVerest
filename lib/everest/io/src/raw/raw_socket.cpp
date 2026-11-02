// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <cerrno>
#include <chrono>
#include <everest/io/raw/raw_socket.hpp>
#include <everest/io/socket/socket.hpp>
#include <exception>
#include <netdb.h>
#include <thread>

namespace everest::lib::io::raw {

bool raw_socket::open(std::string const& if_name) {
    m_error.clear();
    // A reopen that throws must not leave the previous descriptor in place.
    m_fd.close();
    try {
        auto socket = socket::open_raw_promiscuous_socket(if_name);
        socket::set_non_blocking(socket);
        // A device that holds frames then fills this socket's send memory and the fd stops being
        // writable (EAGAIN), so the client waits instead of feeding a queue it cannot see.
        socket::set_socket_send_buffer_to_min(socket);
        m_fd = std::move(socket);
        return socket::get_pending_error(m_fd) == 0;
    } catch (...) {
    }
    return false;
}

bool raw_socket::tx(PayloadT& payload) {
    if (not is_open()) {
        return false;
    }

    auto status = ::send(m_fd, payload.data(), payload.size(), 0);
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

bool raw_socket::rx(PayloadT& buffer) {
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

int raw_socket::get_fd() const {
    return m_fd;
}

int raw_socket::get_error() const {
    return m_error.report(m_fd);
}

bool raw_socket::is_open() const {
    return m_fd.is_fd();
}

void raw_socket::close() {
    m_fd.close();
}

} // namespace everest::lib::io::raw
