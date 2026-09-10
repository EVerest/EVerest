// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#pragma once
#include <everest/io/event/fd_event_sync_interface.hpp>
#include <everest/io/event/unique_fd.hpp>
#include <everest/io/tls/tls_server.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

// Opaque: libtls stays out of the public headers, see tls_socket_base.hpp.
namespace tls {
class Server;
}

namespace everest::lib::io::tls {

// Listening TLS socket. Owns a ::tls::Server and a non-blocking listen fd, register it with an
// fd_event_handler to drive accept events on the loop. Each accepted connection is wrapped in a
// tls_server and handed to the accept callback, which takes ownership of the unique_ptr.
class tls_listener : public event::fd_event_sync_interface {
public:
    // Defined in tls_listener_config.hpp.
    struct Config;

    using accept_cb =
        std::function<void(std::unique_ptr<tls_server> conn, std::string peer_ip, std::uint16_t peer_port)>;
    using error_cb = std::function<void(int, std::string const&)>;

    // Creates a non-blocking listen socket and builds the SSL_CTX. Throws
    // std::runtime_error on a system-call or libtls init failure.
    explicit tls_listener(Config cfg);

    tls_listener(tls_listener const&) = delete;
    tls_listener(tls_listener&&) = delete;
    tls_listener& operator=(tls_listener const&) = delete;
    tls_listener& operator=(tls_listener&&) = delete;

    // Out of line: the unique_ptr needs ::tls::Server complete.
    ~tls_listener() override;

    // Fires synchronously inside sync(), before the handshake: the receiver attaches an rx handler
    // and registers the connection so the loop drives the handshake.
    void set_accept_callback(accept_cb cb) {
        m_cb = std::move(cb);
    }

    // The only way to observe a failed accept: sync()'s status is discarded by the sync-interface
    // registration, so a listener that accepts nothing is otherwise indistinguishable from an idle
    // one. Never called with code 0.
    void set_error_handler(error_cb cb) {
        m_error = std::move(cb);
    }

    int get_poll_fd() override {
        return static_cast<int>(m_listen_fd);
    }

    event::sync_status sync() override;

    // Resolved after bind, so this is where an ephemeral bind_port lands.
    std::uint16_t listen_port() const {
        return m_listen_port;
    }

private:
    // Spend the reserved descriptor to accept and immediately drop one queued connection. Under
    // descriptor exhaustion the connection stays queued and the listen fd stays readable, so
    // returning without draining it would spin the loop on every poll.
    void shed_queued_connection(int accept_errno);
    void report_error(int code, std::string const& what);

    std::unique_ptr<::tls::Server> m_server;
    event::unique_fd m_listen_fd;
    // Held open only so it can be released to make room for the shed above.
    event::unique_fd m_reserve_fd;
    std::uint16_t m_listen_port{0};
    accept_cb m_cb;
    error_cb m_error;
};

} // namespace everest::lib::io::tls
