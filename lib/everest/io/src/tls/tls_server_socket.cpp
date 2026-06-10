// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/tls/tls_server_socket.hpp>

#include <utility>

namespace everest::lib::io::tls {

bool tls_server_socket::open(std::unique_ptr<::tls::ServerConnection> conn) {
    m_conn = std::move(conn);
    m_handshake_done = false;
    m_desired = event::poll_events::read;
    reset_error_state();
    return static_cast<bool>(m_conn);
}

::tls::Connection* tls_server_socket::connection() const {
    return m_conn.get();
}

::tls::Connection::result_t tls_server_socket::step_handshake() {
    return m_conn->accept(0);
}

void tls_server_socket::reset_connection() {
    m_conn.reset();
}

} // namespace everest::lib::io::tls
