// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#pragma once
#include <everest/io/event/fd_event_sync_interface.hpp>
#include <everest/io/event/unique_fd.hpp>
#include <everest/io/tls/tls_server_connection.hpp>
#include <everest/tls/tls.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace everest::lib::io::tls {

/**
 * @brief Listening TLS socket that yields event-loop-ready connections.
 *
 * Owns an internal ::tls::Server (initialised from the Config supplied in the
 * constructor) and a non-blocking listen fd. Implements fd_event_sync_interface
 * so registering with an fd_event_handler is enough to drive accept events on
 * the loop thread — no separate init() step in user code.
 *
 * For each accepted TCP connection sync() wraps the fd in a
 * tls_server_connection and passes it to the accept callback. The receiver of
 * the unique_ptr owns the connection's lifetime.
 */
class tls_listener : public event::fd_event_sync_interface {
public:
    /** @brief Configuration bundle for the underlying TLS server. */
    struct Config {
        ::tls::Server::config_t tls{}; //!< chains, ciphers, verify mode, …
        std::string bind_addr;         //!< IPv4 or IPv6 address to bind to
        std::uint16_t bind_port{0};    //!< port to bind to; 0 selects ephemeral
        bool ipv6_only{false};         //!< when true, AF_INET6 + IPV6_V6ONLY=1
    };

    /** @brief Callback invoked for each accepted TLS connection. */
    using accept_cb =
        std::function<void(std::unique_ptr<tls_server_connection> conn, std::string peer_ip, std::uint16_t peer_port)>;

    /**
     * @brief Construct, bind, and initialise the embedded TLS server.
     *
     * Creates a non-blocking listen socket, plugs it into the supplied
     * tls::Server::config_t, and calls server.init() to build the SSL_CTX.
     * Throws std::runtime_error on any system call failure or if the libtls
     * initialisation fails.
     *
     * @param cfg Configuration bundle (tls config + bind address/port).
     */
    explicit tls_listener(Config cfg);

    tls_listener(tls_listener const&) = delete;
    tls_listener(tls_listener&&) = delete;
    tls_listener& operator=(tls_listener const&) = delete;
    tls_listener& operator=(tls_listener&&) = delete;

    /**
     * @brief Set the callback invoked once per accepted connection.
     *
     * The callback fires synchronously inside sync() on the event-loop thread.
     * The TLS handshake has not yet completed when the callback runs; the
     * receiver should attach an rx handler to the connection and register it
     * with the event handler so the loop drives the handshake.
     *
     * @param cb Callback receiving the new connection, peer IP and port.
     */
    void set_accept_callback(accept_cb cb) {
        m_cb = std::move(cb);
    }

    /**
     * @brief Return the listen fd for registration with fd_event_handler.
     */
    int get_poll_fd() override {
        return static_cast<int>(m_listen_fd);
    }

    /**
     * @brief Accept one pending connection and fire the accept callback.
     */
    event::sync_status sync() override;

    /**
     * @brief Resolved local port (useful when bind_port was 0).
     */
    std::uint16_t listen_port() const {
        return m_listen_port;
    }

private:
    ::tls::Server m_server;
    event::unique_fd m_listen_fd;
    std::uint16_t m_listen_port{0};
    accept_cb m_cb;
};

} // namespace everest::lib::io::tls
