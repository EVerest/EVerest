// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#pragma once
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/tls/tls_endpoint_base.hpp>
#include <everest/io/tls/tls_server_socket.hpp>

#include <memory>

// Opaque: libtls stays out of the public headers, see tls_socket_base.hpp.
namespace tls {
class ServerConnection;
}

namespace everest::lib::io::tls {

// Event-loop-driven TLS server connection: register it with an fd_event_handler to drive it. The
// accept handshake runs on the loop, kicked by the incoming ClientHello, so no worker thread is
// needed.
//
// Single-use: wraps exactly one accepted connection and never reconnects, since an accepted
// connection cannot be replayed. On disconnect or error the owner must drop the unique_ptr.
class tls_server : public tls_endpoint_base<tls_server_socket> {
public:
    explicit tls_server(std::unique_ptr<::tls::ServerConnection> conn);

    tls_server(tls_server const&) = delete;
    tls_server(tls_server&&) = delete;
    tls_server& operator=(tls_server const&) = delete;
    tls_server& operator=(tls_server&&) = delete;
    ~tls_server() override;

private:
    bool start(event::fd_event_handler& handler) override;
    void stop() override;
};

} // namespace everest::lib::io::tls
