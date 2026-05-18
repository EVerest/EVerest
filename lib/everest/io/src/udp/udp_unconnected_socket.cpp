// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/udp/udp_unconnected_socket.hpp>

#include <exception>
#include <utility>

#include <sys/socket.h>

#include <everest/io/socket/socket.hpp>

namespace everest::lib::io::udp {

bool udp_unconnected_socket::open(endpoint target, std::string iface) {
    try {
        m_owned_udp_fd = socket::open_udp_unconnected_socket(target, iface);
        m_target = std::move(target);
        return socket::get_pending_error(m_owned_udp_fd) == 0;
    } catch (const std::exception&) {
    }
    return false;
}

bool udp_unconnected_socket::tx(udp_payload const& payload) {
    if (not m_owned_udp_fd.is_fd()) {
        return false;
    }

    const auto nbytes =
        ::sendto(m_owned_udp_fd, payload.buffer.data(), payload.size(), 0, m_target.sa(), m_target.sa_len());
    return nbytes == static_cast<ssize_t>(payload.size());
}

bool udp_unconnected_socket::rx(udp_payload& payload) {
    if (not m_owned_udp_fd.is_fd()) {
        return false;
    }

    sockaddr_storage src{};
    socklen_t src_len = sizeof(src);
    const auto nbytes = ::recvfrom(m_owned_udp_fd, m_rx_buffer.data(), m_rx_buffer.size(), 0,
                                   reinterpret_cast<sockaddr*>(&src), &src_len);
    if (nbytes < 0) {
        return false;
    }

    payload.set_message(m_rx_buffer.data(), static_cast<size_t>(nbytes));
    m_last_source = endpoint(src, src_len);
    return true;
}

int udp_unconnected_socket::get_fd() const {
    return m_owned_udp_fd;
}

int udp_unconnected_socket::get_error() const {
    return socket::get_pending_error(m_owned_udp_fd);
}

} // namespace everest::lib::io::udp
