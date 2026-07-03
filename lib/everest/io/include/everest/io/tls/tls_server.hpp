// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#pragma once
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/tls/tls_endpoint_base.hpp>
#include <everest/io/tls/tls_server_socket.hpp>
#include <everest/tls/tls.hpp>

#include <memory>

namespace everest::lib::io::tls {

// Event-loop-driven TLS server connection: registering it with an fd_event_handler
// drives it. The accept handshake runs on the loop (accept(0)), kicked by the
// incoming ClientHello, so no worker thread and no TCP connect are needed.
//
// Single-use: a tls_server wraps exactly ONE accepted connection (injected via the
// ctor) and does not reconnect (an accepted connection cannot be replayed). On
// disconnect or error the owner must drop the unique_ptr<tls_server>; the error
// path tears the connection down and does not re-open it.
class tls_server : public tls_endpoint_base<tls_server_socket> {
public:
    explicit tls_server(std::unique_ptr<::tls::ServerConnection> conn);

    tls_server(tls_server const&) = delete;
    tls_server(tls_server&&) = delete;
    tls_server& operator=(tls_server const&) = delete;
    tls_server& operator=(tls_server&&) = delete;
    ~tls_server() override;

private:
    void start(event::fd_event_handler& handler) override;
    void stop() override;
};

} // namespace everest::lib::io::tls
