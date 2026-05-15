// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/tls/tls_server_connection.hpp>

#include <utility>

namespace everest::lib::io::tls {

tls_server_connection::tls_server_connection(::tls::Server::ConnectionPtr conn) :
    m_sock(std::move(conn)), m_fd(m_sock.get_fd()) {
}

tls_server_connection::~tls_server_connection() {
    if (m_handler != nullptr) {
        unregister_events(*m_handler);
    }
}

void tls_server_connection::set_rx_handler(cb_rx cb) {
    m_rx = std::move(cb);
}

void tls_server_connection::set_close_handler(cb_close cb) {
    m_close = std::move(cb);
}

bool tls_server_connection::tx(PayloadT payload) {
    if (m_closed || !m_sock.is_open()) {
        return false;
    }
    if (m_handler == nullptr) {
        m_tx_queue.push(std::move(payload));
        return true;
    }
    m_handler->add_action([this, data = std::move(payload)]() mutable {
        if (m_closed) {
            return;
        }
        m_tx_queue.push(std::move(data));
        if (m_fd >= 0 && m_handler != nullptr) {
            m_handler->modify_event_handler(m_fd, event::poll_events::write, event::event_modification::add);
        }
    });
    return true;
}

bool tls_server_connection::register_events(event::fd_event_handler& handler) {
    if (m_fd < 0 || m_handler != nullptr) {
        return false;
    }
    m_handler = &handler;
    return handler.register_event_handler(
        m_fd, [this](event::fd_event_handler::event_list const& events) { handle_event(events); },
        event::poll_events::read);
}

bool tls_server_connection::unregister_events(event::fd_event_handler& handler) {
    if (m_handler == nullptr || m_fd < 0) {
        return false;
    }
    const bool ok = handler.remove_event_handler(m_fd);
    m_handler = nullptr;
    return ok;
}

void tls_server_connection::close_and_unregister() {
    if (m_closed) {
        return;
    }
    m_closed = true;
    if (m_handler != nullptr) {
        unregister_events(*m_handler);
    }
    m_sock.close();
}

void tls_server_connection::handle_event(event::fd_event_handler::event_list const& events) {
    if (m_closed) {
        return;
    }

    if (!m_sock.handshake_complete()) {
        if (!m_sock.handshake_step()) {
            close_and_notify();
            return;
        }
        select_events(m_sock.desired_events());
        return;
    }

    if (events & event::poll_events::read) {
        PayloadT buf;
        if (m_sock.rx(buf)) {
            if (m_rx) {
                m_rx(buf, *this);
            }
        } else if (!m_sock.is_open()) {
            close_and_notify();
            return;
        }
    }

    if (events & event::poll_events::write) {
        flush_tx_queue();
        if (!m_sock.is_open()) {
            close_and_notify();
            return;
        }
    }

    select_events_for_io();
}

void tls_server_connection::flush_tx_queue() {
    while (!m_tx_queue.empty()) {
        if (!m_sock.tx(m_tx_queue.front())) {
            return;
        }
        m_tx_queue.pop();
    }
}

void tls_server_connection::select_events(event::poll_events desired) {
    if (m_handler == nullptr || m_fd < 0) {
        return;
    }
    m_handler->modify_event_handler(m_fd, event::poll_events::read,
                                    desired == event::poll_events::read ? event::event_modification::add
                                                                        : event::event_modification::remove);
    m_handler->modify_event_handler(m_fd, event::poll_events::write,
                                    desired == event::poll_events::write ? event::event_modification::add
                                                                         : event::event_modification::remove);
}

void tls_server_connection::select_events_for_io() {
    select_events(m_tx_queue.empty() ? event::poll_events::read : event::poll_events::write);
}

void tls_server_connection::close_and_notify() {
    if (m_closed) {
        return;
    }
    m_closed = true;
    if (m_handler != nullptr) {
        unregister_events(*m_handler);
    }
    m_sock.close();
    if (m_close) {
        m_close(*this);
    }
}

} // namespace everest::lib::io::tls
