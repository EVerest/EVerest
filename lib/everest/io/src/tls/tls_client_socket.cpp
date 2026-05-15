// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/tls/tls_client_socket.hpp>

#include <algorithm>
#include <cstring>

namespace everest::lib::io::tls {

namespace {

bool init_client(std::unique_ptr<::tls::Client>& client, ::tls::Client::config_t const& tls_cfg) {
    client = std::make_unique<::tls::Client>();
    if (!client->init(tls_cfg)) {
        client.reset();
        return false;
    }
    return true;
}

} // namespace

bool tls_client_socket::open(Config cfg, std::string const& remote_host, std::uint16_t remote_port) {
    m_cfg = std::move(cfg);
    if (!init_client(m_client, m_cfg.tls)) {
        return false;
    }
    if (!m_tcp.open(remote_host, remote_port)) {
        return false;
    }
    const int timeout = m_cfg.tls.io_timeout_ms > 0 ? m_cfg.tls.io_timeout_ms : 5000;
    return finish_handshake(m_tcp.get_fd(), timeout);
}

bool tls_client_socket::setup(Config cfg, std::string const& remote_host, std::uint16_t remote_port, int timeout_ms) {
    m_cfg = std::move(cfg);
    if (!init_client(m_client, m_cfg.tls)) {
        return false;
    }
    return m_tcp.setup(remote_host, remote_port, timeout_ms);
}

void tls_client_socket::connect(std::function<void(bool, int)> const& cb) {
    m_tcp.connect([this, cb](bool ok, int fd) {
        if (!ok) {
            cb(false, -1);
            return;
        }
        const int timeout = m_cfg.tls.io_timeout_ms > 0 ? m_cfg.tls.io_timeout_ms : 5000;
        if (!finish_handshake(fd, timeout)) {
            cb(false, -1);
            return;
        }
        cb(true, m_conn->socket());
    });
}

bool tls_client_socket::finish_handshake(int tcp_fd, int timeout_ms) {
    m_conn = m_client->wrap_connecting_fd(tcp_fd, m_cfg.host_for_sni.c_str());
    if (!m_conn) {
        m_last_error = -1;
        return false;
    }

    using result_t = ::tls::Connection::result_t;
    const auto res = m_conn->connect(timeout_ms);

    if (res == result_t::success) {
        m_handshake_done = true;
        m_desired = event::poll_events::read;
        return true;
    }

    m_last_error = -1;
    m_conn.reset();
    return false;
}

bool tls_client_socket::tx(PayloadT& payload) {
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

bool tls_client_socket::rx(PayloadT& buffer) {
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

int tls_client_socket::get_fd() const {
    if (m_conn) {
        return m_conn->socket();
    }
    return m_tcp.get_fd();
}

int tls_client_socket::get_error() const {
    if (m_last_error != 0) {
        return m_last_error;
    }
    if (!m_conn) {
        return m_tcp.get_error();
    }
    return 0;
}

bool tls_client_socket::is_open() const {
    return static_cast<bool>(m_conn);
}

void tls_client_socket::close() {
    if (m_conn) {
        m_conn->shutdown(0);
        m_conn.reset();
    }
    m_tcp.close();
    m_desired = event::poll_events::read;
    m_handshake_done = false;
}

event::poll_events tls_client_socket::desired_events() const {
    return m_desired;
}

} // namespace everest::lib::io::tls
