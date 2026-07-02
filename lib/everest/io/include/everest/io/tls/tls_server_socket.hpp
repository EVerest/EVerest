// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#pragma once
#include <everest/io/tls/tls_socket_base.hpp>
#include <everest/tls/tls.hpp>

#include <memory>

namespace everest::lib::io::tls {

// TLS server connection policy. The accepted connection is injected once via
// open(); the event loop then drives accept(0) until handshake_complete(), then
// tx()/rx(). Shared logic lives in tls_socket_base; this class adds the three
// socket-base hooks and holds the accepted ServerConnection.
class tls_server_socket : public tls_socket_base<tls_server_socket> {
public:
    tls_server_socket() = default;

    // Inject an accepted connection (taken by value; moved into m_conn).
    // true when a non-null connection was stored.
    bool open(std::unique_ptr<::tls::ServerConnection> conn);

    tls_server_socket(tls_server_socket const&) = delete;
    tls_server_socket(tls_server_socket&&) = default;
    tls_server_socket& operator=(tls_server_socket const&) = delete;
    tls_server_socket& operator=(tls_server_socket&&) = default;
    ~tls_server_socket() = default;

    // tls_socket_base hooks (public so the base can call them without friendship).
    ::tls::Connection* connection() const;
    ::tls::Connection::result_t step_handshake(); // one non-blocking accept(0)
    void reset_connection();

private:
    std::unique_ptr<::tls::ServerConnection> m_conn;
};

} // namespace everest::lib::io::tls
