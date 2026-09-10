// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#pragma once
#include <everest/io/tls/tls_socket_base.hpp>

#include <functional>
#include <memory>

// Opaque: libtls stays out of the public headers, see tls_socket_base.hpp.
namespace tls {
class ServerConnection;
}

namespace everest::lib::io::tls {

// TLS server connection policy. The accepted connection is injected once via open(), the event
// loop then drives accept(0) until handshake_complete(), then tx()/rx(). Shared logic lives in
// tls_socket_base.
class tls_server_socket : public tls_socket_base<tls_server_socket> {
public:
    tls_server_socket() = default;

    // False when conn is null.
    bool open(std::unique_ptr<::tls::ServerConnection> conn);

    tls_server_socket(tls_server_socket const&) = delete;
    tls_server_socket& operator=(tls_server_socket const&) = delete;

    // Out of line: the unique_ptr needs ::tls::ServerConnection complete.
    tls_server_socket(tls_server_socket&&) noexcept;
    tls_server_socket& operator=(tls_server_socket&&) noexcept;
    ~tls_server_socket();

    // tls_socket_base hooks (public so the base can call them without friendship).
    ::tls::Connection* connection() const;
    io_result step_handshake(); // one non-blocking accept(0)
    void reset_connection();

    // Hand the connection, the fd's BIO_CLOSE owner, to a closer so the fd stays open until the
    // closer runs. Used to defer the close past the handler removal. No TLS shutdown, the
    // connection has faulted.
    std::function<void()> release_closer();

private:
    std::unique_ptr<::tls::ServerConnection> m_conn;
};

} // namespace everest::lib::io::tls
