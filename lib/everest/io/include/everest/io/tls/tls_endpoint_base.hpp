// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#pragma once
#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/fd_event_register_interface.hpp>

#include <cerrno>
#include <cstdint>
#include <functional>
#include <queue>
#include <string>
#include <sys/socket.h>
#include <vector>

namespace everest::lib::io::tls {

// Base for the loop-driven TLS endpoints (client and server). Wires a single TLS
// connection flat onto an fd_event_handler (the charge_bridge pattern) and drives
// the handshake on the loop inside its own dispatch lambda, so the shared handler
// never sees handshake state. tx() enqueues a payload and notifies an internal
// event_fd, which arms POLLOUT on the connection fd.
//
// Templated on the concrete socket (tls_client_socket / tls_server_socket) and
// owns it by value, so the socket's TCP-aware get_fd()/get_error()/close() resolve
// under static typing: no slicing, no virtual dispatch on the rx/tx path. Derived
// supplies only start() (kick the connection) and stop() (tear it down).
template <class Socket> class tls_endpoint_base : public event::fd_event_register_interface {
public:
    using PayloadT = std::vector<std::uint8_t>;
    // The endpoint passes itself so an rx handler can echo via tx() without naming
    // the concrete derived type.
    using cb_rx = std::function<void(PayloadT const&, tls_endpoint_base<Socket>&)>;

    ~tls_endpoint_base() override = default;

    void set_rx_handler(cb_rx handler) {
        m_rx = std::move(handler);
    }

    // Enqueue a payload and arm POLLOUT. Payloads enqueued before the handshake
    // completes are held in the queue and flushed once it does; tx() only
    // rejects after the endpoint has errored.
    bool tx(PayloadT const& payload) {
        if (m_errored) {
            return false;
        }
        m_tx_buffer.push(payload);
        m_tx_notify.notify();
        return true;
    }

    // Fired once, after the handshake completes (client and server).
    void set_on_ready_action(std::function<void()> action) {
        m_on_ready = std::move(action);
    }

    void set_error_handler(std::function<void(int, std::string const&)> handler) {
        m_error = std::move(handler);
    }

    bool register_events(event::fd_event_handler& handler) override {
        // Idempotent: a second register without an intervening unregister is
        // rejected. Re-running start() would, for the client, move-assign a
        // std::thread onto the still-joinable connect worker and std::terminate.
        if (m_handler != nullptr) {
            return false;
        }
        m_handler = &handler;
        auto result = handler.register_event_handler(&m_tx_notify, [this]() {
            // The client connection fd appears only after the async TCP connect
            // completes, so arm POLLOUT only once it is live.
            if (m_fd >= 0) {
                m_handler->modify_event_handler(m_fd, event::poll_events::write, event::event_modification::add);
            }
        });
        start(handler);
        return result;
    }

    bool unregister_events(event::fd_event_handler& handler) override {
        auto result = true;
        if (m_fd >= 0) {
            result = handler.remove_event_handler(m_fd) && result;
            m_fd = -1;
        }
        result = handler.unregister_event_handler(&m_tx_notify) && result;
        stop();
        m_handler = nullptr;
        return result;
    }

protected:
    // Client: async TCP connect + handshake drive. Server: arm the accepted fd.
    virtual void start(event::fd_event_handler& handler) = 0;
    // Client: unregister its connect event then close. Server: close.
    virtual void stop() = 0;

    // Install the connection-fd dispatch lambda armed for `initial`: error/hangup
    // -> fail(), handshake-in-progress -> drive_handshake(), otherwise rx/tx.
    void register_connection_fd(event::fd_event_handler& handler, int fd, event::poll_events initial) {
        m_fd = fd;
        handler.register_event_handler(
            fd,
            [this, fd](auto events) {
                using namespace event;
                if (events.count(poll_events::error) || events.count(poll_events::hungup)) {
                    fail(resolve_poll_error(fd));
                    return;
                }
                if (not m_socket.handshake_complete()) {
                    drive_handshake(fd);
                    return;
                }
                if (events.count(poll_events::read)) {
                    flush_rx();
                }
                if (events.count(poll_events::write)) {
                    flush_tx(fd);
                }
            },
            initial);
    }

    void drive_handshake(int fd) {
        if (not m_socket.handshake_step()) {
            fail(m_socket.get_error());
            return;
        }
        if (m_socket.handshake_complete()) {
            maybe_fire_ready();
            arm_for(fd, event::poll_events::read);
            // Payloads queued before the handshake completed consumed their
            // one-shot tx-notify while the fd was not armed for write. Re-fire
            // the notify so its handler re-arms POLLOUT through the usual path.
            if (not m_tx_buffer.empty()) {
                m_tx_notify.notify();
            }
            return;
        }
        arm_for(fd, m_socket.desired_events());
    }

    void flush_rx() {
        if (m_socket.rx(m_rx_data)) {
            if (m_rx) {
                m_rx(m_rx_data, *this);
            }
        } else if (not m_socket.is_open()) {
            fail(m_socket.get_error());
        }
    }

    // Drain one queued payload; drop POLLOUT when the queue empties. A partial
    // write keeps the front payload (written prefix already erased) and POLLOUT armed.
    void flush_tx(int fd) {
        if (m_tx_buffer.empty()) {
            if (m_handler != nullptr) {
                m_handler->modify_event_handler(fd, event::poll_events::write, event::event_modification::remove);
            }
            return;
        }
        auto& front = m_tx_buffer.front();
        if (m_socket.tx(front)) {
            m_tx_buffer.pop();
            if (m_tx_buffer.empty() && m_handler != nullptr) {
                m_handler->modify_event_handler(fd, event::poll_events::write, event::event_modification::remove);
            }
        } else if (not m_socket.is_open()) {
            fail(m_socket.get_error());
        }
    }

    void maybe_fire_ready() {
        if (m_ready_fired || not m_on_ready) {
            return;
        }
        m_ready_fired = true;
        if (m_handler != nullptr) {
            m_handler->add_action(m_on_ready);
        }
    }

    // fail() runs from inside the connection-fd dispatch lambda, which poll_impl
    // invokes through a reference into its event map. Removing the fd synchronously
    // would destroy the executing std::function (use-after-free) and resize the
    // pollfds vector mid-iteration, so defer it via add_action() (mirrors
    // generic_fd_event_client error_handler). Idempotent within a connection.
    // Resolve the error code for an EPOLLERR/EPOLLHUP dispatch. The socket's
    // own error is preferred; when it is 0 (a live connection that has not yet
    // failed a TLS op, e.g. a peer RST after the handshake) fall back to the
    // socket-level SO_ERROR, then to ECONNRESET for a pure hangup. Never resolves
    // to 0, so the error callback is never invoked with a code every caller ignores.
    int resolve_poll_error(int fd) const {
        int err = m_socket.get_error();
        if (err != 0) {
            return err;
        }
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) == 0 && so_error != 0) {
            return so_error;
        }
        return ECONNRESET;
    }

    void fail(int error_code) {
        if (m_errored) {
            return;
        }
        // Backstop the invariant that the error callback never sees 0: shipped
        // examples branch on `if (err != 0)`, so a 0 would be silently dropped
        // while m_errored latches the endpoint dead.
        if (error_code == 0) {
            error_code = ECONNRESET;
        }
        m_errored = true;
        if (m_error) {
            m_error(error_code, m_socket.get_error_string());
        }
        if (m_fd >= 0 && m_handler != nullptr) {
            // Defer BOTH the handler removal and the fd close, ordered
            // remove-then-close, so the fd number stays reserved until it is out
            // of the handler. A synchronous close would free the number while the
            // deferred remove-by-number is still pending, letting an accept in the
            // same poll batch recycle it and the stale remove strand it. The
            // connection is moved into the action (never `this`-captured) so a
            // destroyed endpoint cannot use-after-free.
            auto close_conn = m_socket.release_closer();
            m_handler->add_action([handler = m_handler, fd = m_fd, close_conn = std::move(close_conn)]() mutable {
                handler->remove_event_handler(fd);
                close_conn();
            });
            m_fd = -1;
        } else {
            // No live connection fd (e.g. failure before the fd was armed): the
            // socket can be closed synchronously.
            m_socket.close();
        }
    }

    // Arm the connection fd for exactly one of read/write.
    void arm_for(int fd, event::poll_events desired) {
        if (m_handler == nullptr) {
            return;
        }
        m_handler->modify_event_handler(fd, event::poll_events::read,
                                        desired == event::poll_events::read ? event::event_modification::add
                                                                            : event::event_modification::remove);
        m_handler->modify_event_handler(fd, event::poll_events::write,
                                        desired == event::poll_events::write ? event::event_modification::add
                                                                             : event::event_modification::remove);
    }

    Socket m_socket;
    event::fd_event_handler* m_handler{nullptr};
    event::event_fd m_tx_notify;
    std::queue<PayloadT> m_tx_buffer;
    cb_rx m_rx;
    std::function<void()> m_on_ready;
    std::function<void(int, std::string const&)> m_error;
    bool m_ready_fired{false};
    bool m_errored{false};
    int m_fd{-1};

private:
    PayloadT m_rx_data;
};

} // namespace everest::lib::io::tls
