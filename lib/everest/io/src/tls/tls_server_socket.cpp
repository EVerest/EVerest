// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/tls/tls_server_socket.hpp>

#include <algorithm>
#include <cstring>

namespace everest::lib::io::tls {

tls_server_socket::tls_server_socket(::tls::Server::ConnectionPtr conn) : m_conn(std::move(conn)) {
}

bool tls_server_socket::handshake_step() {
    if (!m_conn) {
        return false;
    }
    if (m_handshake_done) {
        // No more handshake work needed; caller checks handshake_complete().
        return false;
    }

    using result_t = ::tls::Connection::result_t;
    const auto res = m_conn->accept(0);
    switch (res) {
    case result_t::success:
        m_handshake_done = true;
        m_desired = event::poll_events::read;
        return true;
    case result_t::want_read:
        m_desired = event::poll_events::read;
        return true;
    case result_t::want_write:
        m_desired = event::poll_events::write;
        return true;
    case result_t::closed:
    case result_t::timeout:
    default:
        m_last_error = -1;
        m_conn.reset();
        return false;
    }
}

bool tls_server_socket::handshake_complete() const {
    return m_handshake_done;
}

bool tls_server_socket::tx(PayloadT& payload) {
    if (payload.empty()) {
        return true;
    }
    if (!m_conn) {
        return false;
    }

    std::size_t written = 0;
    using result_t = ::tls::Connection::result_t;
    const auto res = m_conn->write(reinterpret_cast<std::byte const*>(payload.data()), payload.size(), written, 0);

    switch (res) {
    case result_t::success:
        payload.erase(payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(written));
        return payload.empty();
    case result_t::want_read:
        if (written > 0) {
            payload.erase(payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(written));
        }
        m_desired = event::poll_events::read;
        return false;
    case result_t::want_write:
        if (written > 0) {
            payload.erase(payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(written));
        }
        m_desired = event::poll_events::write;
        return false;
    case result_t::closed:
    case result_t::timeout:
    default:
        m_last_error = -1;
        m_conn.reset();
        return false;
    }
}

bool tls_server_socket::rx(PayloadT& buffer) {
    if (!m_conn) {
        return false;
    }

    std::byte tmp[4096];
    std::size_t n = 0;
    using result_t = ::tls::Connection::result_t;
    const auto res = m_conn->read(tmp, sizeof tmp, n, 0);

    buffer.clear();
    switch (res) {
    case result_t::success:
        buffer.assign(reinterpret_cast<std::uint8_t*>(tmp), reinterpret_cast<std::uint8_t*>(tmp) + n);
        m_desired = event::poll_events::read;
        return true;
    case result_t::want_read:
        m_desired = event::poll_events::read;
        return false;
    case result_t::want_write:
        m_desired = event::poll_events::write;
        return false;
    case result_t::closed:
    case result_t::timeout:
    default:
        m_last_error = -1;
        m_conn.reset();
        return false;
    }
}

int tls_server_socket::get_fd() const {
    return m_conn ? m_conn->socket() : -1;
}

int tls_server_socket::get_error() const {
    return m_last_error;
}

bool tls_server_socket::is_open() const {
    return static_cast<bool>(m_conn);
}

void tls_server_socket::close() {
    if (m_conn) {
        m_conn->shutdown(0);
        m_conn.reset();
    }
    m_desired = event::poll_events::read;
    m_handshake_done = false;
}

event::poll_events tls_server_socket::desired_events() const {
    return m_desired;
}

} // namespace everest::lib::io::tls
