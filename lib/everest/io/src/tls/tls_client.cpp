// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/tls/tls_client.hpp>

#include <thread>
#include <utility>

namespace everest::lib::io::tls {

tls_client::tls_client(tls_client_socket::Config cfg, std::string host, std::uint16_t port, int timeout_ms) :
    m_cfg(std::move(cfg)), m_host(std::move(host)), m_port(port), m_timeout_ms(timeout_ms) {
}

void tls_client::start(event::fd_event_handler& handler) {
    // Fired from the connect worker. On success the fd is live: register it armed
    // for write and drive the first handshake step (the client must send its
    // ClientHello — a freshly connected socket has nothing to read yet).
    handler.register_event_handler(&m_connected, [this, &handler]() {
        if (m_connect_ok) {
            register_connection_fd(handler, m_socket.get_fd(), event::poll_events::write);
            drive_handshake(m_socket.get_fd());
        } else {
            fail(m_socket.get_error());
        }
    });

    if (not m_socket.setup(m_cfg, m_host, m_port, m_timeout_ms)) {
        m_connect_ok = false;
        m_connected.notify();
        return;
    }

    // TCP connect on the owned worker thread; TLS handshake on the loop. The
    // worker is joined on teardown (stop()/dtor) before m_socket is closed.
    m_worker = std::thread([this]() {
        m_socket.connect([this](bool ok, int /*fd*/) {
            m_connect_ok = ok;
            m_connected.notify();
        });
    });
}

void tls_client::stop() {
    // Symmetric with start(), which registered m_connected: the base's
    // unregister_events() only removes the connection fd and tx-notify, so the
    // client tears down its own m_connected here (m_handler is still valid).
    if (m_handler != nullptr) {
        m_handler->unregister_event_handler(&m_connected);
    }
    // Join before close(): the worker may still be inside m_socket.connect();
    // joining here (bounded by the connect timeout) guarantees it has stopped
    // touching m_socket before we tear it down.
    if (m_worker.joinable()) {
        m_worker.join();
    }
    m_socket.close();
}

tls_client::~tls_client() {
    // Backstop for a drop while still registered (the README's "connections.clear()"
    // teardown): remove the connection fd, tx-notify and m_connected synchronously
    // so no this-capturing lambda is left in the handler to dispatch into a freed
    // object. unregister_event_handler routes through unregister_events()/stop(),
    // which also joins the worker. A no-op after an explicit unregister (m_handler
    // is then null).
    if (m_handler != nullptr) {
        m_handler->unregister_event_handler(this);
    }
    // Backstop for any path that destroys without stop(): the worker must not
    // outlive *this (it captures this and writes m_connect_ok / m_connected).
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

} // namespace everest::lib::io::tls
