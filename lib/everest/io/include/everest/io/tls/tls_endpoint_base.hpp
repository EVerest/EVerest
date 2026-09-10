// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#pragma once
#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/fd_event_register_interface.hpp>
#include <everest/io/event/timer_fd.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <queue>
#include <string>
#include <utility>
#include <vector>

namespace everest::lib::io::tls {

// Base for the loop-driven TLS server endpoints. Wires one accepted TLS connection onto an
// fd_event_handler and drives the handshake inside its own dispatch lambda, so the shared handler
// never sees handshake state. Derived supplies start() (monitor the accepted fd) and stop().
// The client side instead is an fd_event_client over tls_client_socket, with its own nested handler
// and connect thread. The socket is owned by value, so its get_fd()/get_error()/close() resolve
// statically, with no virtual dispatch on the rx/tx path.
// Member definitions live in tls_endpoint_base_impl.hpp.
template <class Socket> class tls_endpoint_base : public event::fd_event_register_interface {
public:
    using PayloadT = std::vector<std::uint8_t>;
    // Passes the endpoint itself, so an rx handler can tx() without naming the derived type.
    using cb_rx = std::function<void(PayloadT const&, tls_endpoint_base<Socket>&)>;

    /**
     * @var max_buffered_tx_payloads
     * @brief Upper bound of payloads held in the tx buffer, past which \ref tx rejects.
     * @details Counts payloads, not bytes, so the ceiling scales with the size the caller writes.
     */
    static constexpr std::size_t max_buffered_tx_payloads{1024};

    /**
     * @var default_handshake_timeout
     * @brief Upper bound of the accept handshake, matching the client's bound in
     * generic_fd_event_client_impl. Past this the peer stopped talking and the endpoint fails
     * with ETIMEDOUT.
     */
    static constexpr std::chrono::milliseconds default_handshake_timeout{10000};

    ~tls_endpoint_base() override = default;

    // Runs inside the connection dispatch, which keeps reading members after it returns, so it may
    // not destroy the endpoint. The error handler is the callback for that.
    void set_rx_handler(cb_rx handler) {
        m_rx = std::move(handler);
    }

    // Enqueue a payload and add POLLOUT; payloads enqueued during the handshake are flushed once it
    // completes. Rejects after the endpoint errored and at max_buffered_tx_payloads, which a peer
    // that stops reading reaches on its own. A rejection is backpressure, not a failure: the
    // connection stays live and everything already accepted is still delivered. Loop thread only,
    // the queue is not synchronized.
    bool tx(PayloadT const& payload);

    // Fired once, after the accept handshake completes, from run_actions(). Safe to destroy the
    // endpoint from, same as the error handler.
    void set_on_ready_action(std::function<void()> action) {
        m_on_ready = std::move(action);
    }

    // Fired once per connection with a nonzero code, from run_actions() rather than from the
    // dispatch that failed, so the callback may destroy the endpoint. That is the documented way to
    // retire a tls_server, which never reconnects. The report is queued behind the fd teardown, and
    // is lost if the handler dies before it drains.
    void set_error_handler(std::function<void(int, std::string const&)> handler) {
        m_error = std::move(handler);
    }

    // Bound on the whole accept handshake. Effective only before registration, the deadline arms
    // when the connection fd registers. A non-positive value selects default_handshake_timeout.
    void set_handshake_timeout(std::chrono::milliseconds timeout) {
        m_handshake_timeout = timeout > std::chrono::milliseconds::zero() ? timeout : default_handshake_timeout;
    }

    bool register_events(event::fd_event_handler& handler) override;

    bool unregister_events(event::fd_event_handler& handler) override;

protected:
    virtual bool start(event::fd_event_handler& handler) = 0;
    virtual void stop() = 0;

    bool register_connection_fd(event::fd_event_handler& handler, int fd, event::poll_events initial);

    void drive_handshake(int fd);

    void flush_rx(int fd);

    void flush_tx(int fd);

    // Whose would-block m_desired describes; the socket reports one desired event for both
    // directions. `none` means nothing blocked, so m_desired is the idle need and not a wait:
    // reading a completed send as a parked one drops POLLOUT with the queue still to go.
    enum class direction : std::uint8_t {
        receive,
        send,
        none
    };

    // Invariant: read off implies write on, so the descriptor is never left unmonitored.
    bool route_desired_events(int fd, direction blocked);

    void maybe_fire_ready();

    // Error code for an EPOLLERR/EPOLLHUP dispatch. The socket's own error wins; a 0 there is a live
    // connection that has not yet failed a TLS op, e.g. a peer RST after the handshake. Never
    // resolves to 0. Reading SO_ERROR clears it, so fail()'s idempotence keeps this to one call.
    int consume_poll_error(int fd);

    // Idempotent within a connection. Safe from inside a dispatch: the fd teardown and the error
    // report are both deferred, in that order.
    void fail(int error_code);

    // Same, with a text that the socket's own error state cannot describe.
    void fail(int error_code, std::string const& error_text);

    void retire_handshake_deadline();

    // Monitor the connection fd for exactly one of read/write. False means the fd is not monitored for
    // anything, which no caller may treat as success.
    bool monitor_for(int fd, event::poll_events desired);

    // The cache is answered only while the registration still exists, so an unchanged mask cannot
    // report success on a descriptor nothing monitors.
    bool apply_monitoring(int fd, bool read, bool write);

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
    // What the handler currently monitors on m_fd. Written by apply_monitoring and by the seed in
    // register_connection_fd, nowhere else.
    bool m_monitored_read{false};
    bool m_monitored_write{false};
    event::timer_fd m_handshake_timer;
    std::chrono::milliseconds m_handshake_timeout{default_handshake_timeout};
    // One poll batch can carry both the expiry and the event that completes the handshake, and the
    // batch is harvested before dispatch, so the expiry handler needs this beyond the disarm.
    bool m_handshake_deadline_active{false};

private:
    PayloadT m_rx_data;
};

} // namespace everest::lib::io::tls
