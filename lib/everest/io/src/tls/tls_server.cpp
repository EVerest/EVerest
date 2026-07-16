// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/tls/tls_server.hpp>

#include <utility>

namespace everest::lib::io::tls {

tls_server::tls_server(std::unique_ptr<::tls::ServerConnection> conn) {
    m_socket.open(std::move(conn));
}

void tls_server::start(event::fd_event_handler& handler) {
    // The server is kicked by the incoming ClientHello: arm the connection fd
    // for read and let the dispatch lambda drive the accept(0) handshake once
    // bytes arrive. No worker thread and no TCP connect are needed.
    register_connection_fd(handler, m_socket.get_fd(), event::poll_events::read);
}

void tls_server::stop() {
    m_socket.close();
}

tls_server::~tls_server() {
    // Backstop for a drop while still registered (the README's "connections.clear()"
    // teardown): remove the connection fd and tx-notify synchronously so no
    // this-capturing lambda is left in the handler. A no-op after an explicit
    // unregister (m_handler is then null).
    if (m_handler != nullptr) {
        m_handler->unregister_event_handler(this);
    }
}

} // namespace everest::lib::io::tls
