// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#pragma once
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/fd_event_register_interface.hpp>
#include <everest/io/tls/tls_server_socket.hpp>
#include <everest/tls/tls.hpp>

#include <cstdint>
#include <functional>
#include <queue>
#include <vector>

namespace everest::lib::io::tls {

/**
 * @brief Event-loop-driven accepted TLS server connection.
 *
 * Wraps a tls_server_socket and integrates it with an fd_event_handler so the
 * TLS handshake, reads, and writes are all driven by the loop — no manual
 * polling, no manual modify_event_handler. Callers supply a receive callback
 * via set_rx_handler() and queue outgoing data via tx().
 *
 * The connection is constructed by tls_listener::sync() and handed to the
 * accept callback. Register it with the loop via
 * fd_event_handler::register_event_handler(connection) — the handler invokes
 * register_events() per the fd_event_register_interface contract.
 */
class tls_server_connection : public event::fd_event_register_interface {
public:
    /** @brief Payload type for tx/rx operations. */
    using PayloadT = std::vector<std::uint8_t>;

    /** @brief Callback invoked on each receive event. */
    using cb_rx = std::function<void(PayloadT const& payload, tls_server_connection& conn)>;

    /** @brief Callback invoked when the connection closes (handshake or peer). */
    using cb_close = std::function<void(tls_server_connection& conn)>;

    /**
     * @brief Construct from an accepted TLS connection (sync_interface output).
     * @param conn Server::wrap_accepted_fd result; ownership transferred.
     */
    explicit tls_server_connection(::tls::Server::ConnectionPtr conn);

    ~tls_server_connection() override;

    tls_server_connection(tls_server_connection const&) = delete;
    tls_server_connection(tls_server_connection&&) = delete;
    tls_server_connection& operator=(tls_server_connection const&) = delete;
    tls_server_connection& operator=(tls_server_connection&&) = delete;

    /**
     * @brief Set the receive callback fired on each successful rx().
     * @param cb Callback receiving (payload, *this).
     */
    void set_rx_handler(cb_rx cb);

    /**
     * @brief Set the close callback fired once when the connection ends.
     * @param cb Callback receiving (*this).
     */
    void set_close_handler(cb_close cb);

    /**
     * @brief Queue a payload for transmission.
     *
     * The send is dispatched via the event handler's action queue; the bytes
     * are written when the handler reports the fd is writable. Safe to call
     * from any thread or from inside event callbacks.
     *
     * @param payload Bytes to send. Moved into the internal queue.
     * @return true once enqueued; false if already closed.
     */
    bool tx(PayloadT payload);

    /**
     * @brief TLS-shutdown the connection and remove it from the event handler.
     *
     * Idempotent. After this returns, the connection cannot be re-registered.
     */
    void close_and_unregister();

private:
    /**
     * @brief Install read/write callbacks on the event handler.
     *
     * Invoked by fd_event_handler::register_event_handler(connection) per the
     * fd_event_register_interface contract.
     */
    bool register_events(event::fd_event_handler& handler) override;

    /**
     * @brief Remove callbacks from the event handler.
     *
     * Invoked by fd_event_handler::unregister_event_handler(connection).
     * Idempotent.
     */
    bool unregister_events(event::fd_event_handler& handler) override;

    void handle_event(event::fd_event_handler::event_list const& events);
    void select_events(event::poll_events desired);
    void select_events_for_io();
    void flush_tx_queue();
    void close_and_notify();

    tls_server_socket m_sock;
    event::fd_event_handler* m_handler{nullptr};
    int m_fd{-1};
    cb_rx m_rx;
    cb_close m_close;
    std::queue<PayloadT> m_tx_queue;
    bool m_closed{false};
};

} // namespace everest::lib::io::tls
