// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/tls/tls_client_socket.hpp>
#include <everest/io/tls/tls_result.hpp>

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
        // Wrap the connected TCP fd but do NOT perform the TLS handshake here:
        // the event loop drives it via handshake_step() once the fd is armed.
        m_conn = m_client->wrap_connecting_fd(fd, m_cfg.host_for_sni.c_str());
        if (!m_conn) {
            m_last_error = EPROTO;
            cb(false, -1);
            return;
        }
        // After a successful wrap the connection's BIO (BIO_CLOSE) is the sole
        // owner of the fd, so m_tcp surrenders its claim.
        m_tcp.release();
        cb(true, m_conn->socket());
    });
}

::tls::Connection* tls_client_socket::connection() const {
    return m_conn.get();
}

::tls::Connection::result_t tls_client_socket::step_handshake() {
    return m_conn->connect(0);
}

void tls_client_socket::reset_connection() {
    m_conn.reset();
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

void tls_client_socket::close() {
    // Base close() shuts down + releases the connection and resets handshake state
    // (via the connection()/reset_connection() hooks); this adds the TCP fd.
    tls_socket_base::close();
    m_tcp.close();
}

} // namespace everest::lib::io::tls
