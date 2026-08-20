// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/tls/tls_server.hpp>
#include <everest/tls/tls.hpp>

#include <utility>

namespace everest::lib::io::tls {

tls_server::tls_server(std::unique_ptr<::tls::ServerConnection> conn) {
    m_socket.open(std::move(conn));
}

bool tls_server::start(event::fd_event_handler& handler) {
    // The incoming ClientHello kicks the handshake, so read is the only event to monitor.
    return register_connection_fd(handler, m_socket.get_fd(), event::poll_events::read);
}

void tls_server::stop() {
    m_socket.close();
}

tls_server::~tls_server() {
    // Backstop for a drop while still registered: remove the fds synchronously so no
    // this-capturing lambda is left in the handler.
    if (m_handler != nullptr) {
        m_handler->unregister_event_handler(this);
    }
}

} // namespace everest::lib::io::tls
