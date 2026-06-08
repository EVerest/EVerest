// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#pragma once
#include <everest/io/event/fd_event_sync_interface.hpp>
#include <everest/io/event/unique_fd.hpp>
#include <everest/io/tls/tls_server.hpp>
#include <everest/tls/tls.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace everest::lib::io::tls {

// Listening TLS socket. Owns a ::tls::Server and a non-blocking listen fd;
// registering it with an fd_event_handler drives accept events on the loop. Each
// accepted connection is wrapped in a tls_server and handed to the accept
// callback, which takes ownership of the unique_ptr.
class tls_listener : public event::fd_event_sync_interface {
public:
    struct Config {
        ::tls::Server::config_t tls{}; // chains, ciphers, verify mode, …
        std::string bind_addr;
        std::uint16_t bind_port{0}; // 0 selects an ephemeral port
        bool ipv6_only{false};      // when true, AF_INET6 + IPV6_V6ONLY=1
    };

    using accept_cb =
        std::function<void(std::unique_ptr<tls_server> conn, std::string peer_ip, std::uint16_t peer_port)>;

    // Creates a non-blocking listen socket and builds the SSL_CTX. Throws
    // std::runtime_error on a system-call or libtls init failure.
    explicit tls_listener(Config cfg);

    tls_listener(tls_listener const&) = delete;
    tls_listener(tls_listener&&) = delete;
    tls_listener& operator=(tls_listener const&) = delete;
    tls_listener& operator=(tls_listener&&) = delete;

    // The callback fires synchronously inside sync(), BEFORE the handshake: the
    // receiver attaches an rx handler and registers the connection so the loop
    // drives the handshake.
    void set_accept_callback(accept_cb cb) {
        m_cb = std::move(cb);
    }

    int get_poll_fd() override {
        return static_cast<int>(m_listen_fd);
    }

    event::sync_status sync() override;

    // Resolved local port (useful when bind_port was 0).
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
