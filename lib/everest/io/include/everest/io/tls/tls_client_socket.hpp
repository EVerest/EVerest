// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#pragma once
#include <everest/io/tcp/tcp_socket.hpp>
#include <everest/io/tls/tls_socket_base.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace tls {
class Client;
class ClientConnection;
} // namespace tls

namespace everest::lib::io::tls {

// TLS client connection policy: async TCP connect + handshake via setup()/connect(). Shared
// tx/rx/handshake logic lives in tls_socket_base, this class adds the connect machinery and the
// TCP-aware get_fd()/get_error()/close() overrides.
class tls_client_socket : public tls_socket_base<tls_client_socket> {
public:
    static constexpr bool buffer_tx_before_connect{true};

    // Defined in tls_client_config.hpp.
    struct Config;

    tls_client_socket() = default;
    tls_client_socket(tls_client_socket const&) = delete;
    tls_client_socket(tls_client_socket&&) = default;
    tls_client_socket& operator=(tls_client_socket const&) = delete;
    tls_client_socket& operator=(tls_client_socket&&) = default;
    // Closes the TLS session. The event client destroys the policy to tear a connection down and
    // never calls close(), so without this the peer sees a truncated stream.
    ~tls_client_socket();

    // Build the SSL_CTX and record the TCP connect parameters. Starts no connect itself, so it
    // returns quickly. Must be followed by connect(), which does the blocking work.
    bool setup(Config cfg, std::string const& remote_host, std::uint16_t remote_port, int timeout_ms);

    // Wrap the connected fd, without handshaking: the loop drives that. Runs on the
    // fd_event_client connect thread.
    void connect(std::function<void(bool, int)> const& cb);

    // The TLS connection fd once handshaking, the TCP fd otherwise, -1 if neither.
    int get_fd() const;
    int get_error() const;
    void close();

    // tls_socket_base hooks (public so the base can call them without friendship).
    ::tls::Connection* connection() const;
    io_result step_handshake(); // one non-blocking connect(0)
    void reset_connection();

protected:
    // Protected so a unit-test subclass can confirm ownership was surrendered to the TLS
    // connection's BIO (no double-close).
    tcp::tcp_socket m_tcp{};

private:
    std::string m_host_for_sni{};
    std::unique_ptr<::tls::Client> m_client{};
    std::unique_ptr<::tls::ClientConnection> m_conn{};
};

} // namespace everest::lib::io::tls
