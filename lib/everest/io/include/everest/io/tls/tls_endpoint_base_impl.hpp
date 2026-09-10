// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#pragma once
#include <everest/io/socket/socket.hpp>
#include <everest/io/tls/tls_endpoint_base.hpp>

#include <cerrno>
#include <utility>

// Member definitions of tls_endpoint_base. Include to instantiate the base for a Socket of your own.

namespace everest::lib::io::tls {

template <class Socket> bool tls_endpoint_base<Socket>::tx(PayloadT const& payload) {
    if (m_errored || m_tx_buffer.size() >= max_buffered_tx_payloads) {
        return false;
    }
    m_tx_buffer.push(payload);
    m_tx_notify.notify();
    return true;
}

template <class Socket> bool tls_endpoint_base<Socket>::register_events(event::fd_event_handler& handler) {
    if (m_handler != nullptr) {
        return false;
    }
    m_handler = &handler;
    auto result = handler.register_event_handler(&m_tx_notify, [this]() {
        // One-shot and already drained: nothing would retry a failed request.
        if (m_fd >= 0 and not apply_monitoring(m_fd, m_monitored_read, true)) {
            fail(EPROTO);
        }
    });
    m_handshake_timer.set_single_shot(true);
    // Registered before start(): the deadline arms inside register_connection_fd.
    result = handler.register_event_handler(&m_handshake_timer, [this]() {
        // A disarm in this batch already retired the expiry via the tick
        // counter; the flag covers a completion dispatched after it.
        if (not m_handshake_deadline_active) {
            return;
        }
        fail(ETIMEDOUT, "handshake deadline expired");
    }) and result;
    auto started = start(handler);
    return result and started;
}

template <class Socket> bool tls_endpoint_base<Socket>::unregister_events(event::fd_event_handler& handler) {
    auto result = true;
    if (m_fd >= 0) {
        result = handler.remove_event_handler(m_fd) && result;
        m_fd = -1;
    }
    result = handler.unregister_event_handler(&m_tx_notify) && result;
    retire_handshake_deadline();
    result = handler.unregister_event_handler(&m_handshake_timer) && result;
    stop();
    m_handler = nullptr;
    return result;
}

template <class Socket>
bool tls_endpoint_base<Socket>::register_connection_fd(event::fd_event_handler& handler, int fd,
                                                       event::poll_events initial) {
    m_fd = fd;
    m_monitored_read = false;
    m_monitored_write = false;
    const auto registered = handler.register_event_handler(
        fd,
        [this, fd](auto events) {
            using namespace event;
            if (events.count(poll_events::error) || events.count(poll_events::hungup)) {
                fail(consume_poll_error(fd));
                return;
            }
            if (not m_socket.handshake_complete()) {
                drive_handshake(fd);
                return;
            }
            if (events.count(poll_events::read)) {
                flush_rx(fd);
            }
            if (not m_errored and events.count(poll_events::write)) {
                flush_tx(fd);
            }
        },
        initial);
    if (registered) {
        m_monitored_read = initial == event::poll_events::read;
        m_monitored_write = initial == event::poll_events::write;
        // The bound covers the whole handshake: a peer may connect and never send a byte, which
        // generates no event at all, so only a timer can retire the endpoint.
        if (not m_handshake_timer.set_timeout(m_handshake_timeout)) {
            fail(EPROTO, "handshake deadline could not be armed");
            return false;
        }
        m_handshake_deadline_active = true;
    }
    return registered;
}

template <class Socket> void tls_endpoint_base<Socket>::drive_handshake(int fd) {
    if (not m_socket.handshake_step()) {
        fail(m_socket.get_error());
        return;
    }
    if (m_socket.handshake_complete()) {
        retire_handshake_deadline();
        if (not monitor_for(fd, event::poll_events::read)) {
            fail(EPROTO);
            return;
        }
        maybe_fire_ready();
        // Payloads queued during the handshake spent their notify while write was unmonitored.
        if (not m_tx_buffer.empty()) {
            m_tx_notify.notify();
        }
        return;
    }
    if (not monitor_for(fd, m_socket.desired_events())) {
        fail(EPROTO);
    }
}

template <class Socket> void tls_endpoint_base<Socket>::flush_rx(int fd) {
    auto blocked = direction::receive;
    if (m_socket.rx(m_rx_data)) {
        if (m_rx) {
            m_rx(m_rx_data, *this);
        }
        blocked = direction::none;
    } else if (not m_socket.is_open()) {
        fail(m_socket.get_error());
        return;
    }
    // The rx handler may have failed the endpoint, whose fd is already queued for teardown.
    if (not m_errored and not route_desired_events(fd, blocked)) {
        fail(EPROTO);
    }
}

// Every outcome routes: a success moves the socket's need back to read, and a stale mask from an
// earlier would-block would keep POLLIN dropped.
template <class Socket> void tls_endpoint_base<Socket>::flush_tx(int fd) {
    auto blocked = direction::none;
    if (not m_tx_buffer.empty()) {
        auto& front = m_tx_buffer.front();
        if (m_socket.tx(front)) {
            m_tx_buffer.pop();
        } else if (not m_socket.is_open()) {
            fail(m_socket.get_error());
            return;
        } else {
            blocked = direction::send;
        }
    }
    if (not route_desired_events(fd, blocked)) {
        fail(EPROTO);
    }
}

// Read is dropped only for a receive waiting on writability: leaving it on re-drives that receive
// every tick. A send waiting on writability is only a full kernel buffer, so dropping read there
// deadlocks. Write stays off on an idle queue, which would spin the loop on an always-writable fd.
template <class Socket> bool tls_endpoint_base<Socket>::route_desired_events(int fd, direction blocked) {
    const auto wants_write = m_socket.desired_events() == event::poll_events::write;
    const auto rx_wants_write = blocked == direction::receive and wants_write;
    const auto monitor_read = not rx_wants_write;
    const auto monitor_write =
        rx_wants_write or (not m_tx_buffer.empty() and not(blocked == direction::send and not wants_write));
    // Unreachable today: both callers turn a false into fail(EPROTO), so an fd monitored for
    // nothing is loud instead of frozen.
    if (not monitor_read and not monitor_write) {
        return false;
    }
    return apply_monitoring(fd, monitor_read, monitor_write);
}

template <class Socket> void tls_endpoint_base<Socket>::maybe_fire_ready() {
    if (m_ready_fired || not m_on_ready) {
        return;
    }
    m_ready_fired = true;
    if (m_handler != nullptr) {
        m_handler->add_action(m_on_ready);
    }
}

template <class Socket> int tls_endpoint_base<Socket>::consume_poll_error(int fd) {
    int err = m_socket.get_error();
    if (err != 0) {
        return err;
    }
    // Always a real socket here, so the notification kind cannot improve on ECONNRESET.
    return socket::consume_poll_error(fd, event::poll_events::error, ECONNRESET);
}

// A synchronous removal would destroy the executing std::function and resize pollfds mid-iteration.
template <class Socket> void tls_endpoint_base<Socket>::fail(int error_code) {
    fail(error_code, m_socket.get_error_string());
}

template <class Socket> void tls_endpoint_base<Socket>::fail(int error_code, std::string const& error_text) {
    if (m_errored) {
        return;
    }
    retire_handshake_deadline();
    // The error callback never sees 0: callers branch on it, so a 0 leaves a dead endpoint silent.
    if (error_code == 0) {
        error_code = ECONNRESET;
    }
    m_errored = true;
    if (m_fd >= 0 && m_handler != nullptr) {
        // Remove before close, both deferred: a synchronous close lets an accept recycle the fd
        // number and strand the pending remove-by-number.
        auto close_conn = m_socket.release_closer();
        m_handler->add_action([handler = m_handler, fd = m_fd, close_conn = std::move(close_conn)]() mutable {
            handler->remove_event_handler(fd);
            close_conn();
        });
        m_fd = -1;
    } else {
        // Nothing is dispatching, so close synchronously. m_fd stays: apply_monitoring fails
        // without a handler regardless.
        m_socket.close();
    }
    if (not m_error) {
        return;
    }
    if (m_handler == nullptr) {
        // Unreachable by construction: every fail() path runs from a registration this endpoint
        // owns, and register_events sets m_handler before start(). A derived class that makes it
        // reachable inherits a synchronous report, without the guarantee below.
        m_error(error_code, error_text);
        return;
    }
    // Queued after the teardown and holding no `this`, so the callback may destroy the endpoint.
    m_handler->add_action([report = m_error, error_code, text = error_text]() { report(error_code, text); });
}

template <class Socket> void tls_endpoint_base<Socket>::retire_handshake_deadline() {
    m_handshake_deadline_active = false;
    m_handshake_timer.disarm();
}

template <class Socket> bool tls_endpoint_base<Socket>::monitor_for(int fd, event::poll_events desired) {
    return apply_monitoring(fd, desired == event::poll_events::read, desired == event::poll_events::write);
}

template <class Socket> bool tls_endpoint_base<Socket>::apply_monitoring(int fd, bool read, bool write) {
    if (m_handler == nullptr) {
        return false;
    }
    const auto cached = fd == m_fd and m_handler->is_registered(fd);
    auto apply = [&](event::poll_events which, bool want, bool& state) {
        if (cached and state == want) {
            return true;
        }
        const auto ok = m_handler->modify_event_handler(
            fd, which, want ? event::event_modification::add : event::event_modification::remove);
        if (ok and cached) {
            state = want;
        }
        return ok;
    };
    const auto read_ok = apply(event::poll_events::read, read, m_monitored_read);
    const auto write_ok = apply(event::poll_events::write, write, m_monitored_write);
    return read_ok and write_ok;
}

} // namespace everest::lib::io::tls
