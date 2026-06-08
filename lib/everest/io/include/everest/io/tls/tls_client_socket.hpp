// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#pragma once
#include <everest/io/tcp/tcp_socket.hpp>
#include <everest/io/tls/tls_socket_base.hpp>
#include <everest/tls/tls.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace everest::lib::io::tls {

// TLS client connection policy: async TCP connect + handshake via setup()/connect().
// Shared tx/rx/handshake/desired-events logic lives in tls_socket_base; this class
// adds the connect machinery, the TCP-aware get_fd()/get_error()/close() overrides,
// and the three socket-base hooks.
class tls_client_socket : public tls_socket_base<tls_client_socket> {
public:
    struct Config {
        ::tls::Client::config_t tls{};
        // hostname sent in the TLS SNI extension; matched against the server cert
        // when tls.verify_subject_name is set.
        std::string host_for_sni;
    };

    tls_client_socket() = default;
    tls_client_socket(tls_client_socket const&) = delete;
    tls_client_socket(tls_client_socket&&) = default;
    tls_client_socket& operator=(tls_client_socket const&) = delete;
    tls_client_socket& operator=(tls_client_socket&&) = default;
    ~tls_client_socket() = default;

    // Build the SSL_CTX and start a non-blocking TCP connect; returns quickly.
    // Must be followed by connect().
    bool setup(Config cfg, std::string const& remote_host, std::uint16_t remote_port, int timeout_ms);

    // Wrap the connected fd (no handshake; the loop drives that). Runs on a worker
    // thread; calls cb(true, fd) on success, cb(false, -1) on failure.
    void connect(std::function<void(bool, int)> const& cb);

    // TLS connection fd once handshaking; the TCP fd otherwise; -1 if neither.
    int get_fd() const;
    int get_error() const;
    void close();

    // tls_socket_base hooks (public so the base can call them without friendship).
    ::tls::Connection* connection() const;
    ::tls::Connection::result_t step_handshake(); // one non-blocking connect(0)
    void reset_connection();

protected:
    // protected so a unit-test subclass can confirm ownership was surrendered to
    // the TLS connection's BIO (no double-close).
    tcp::tcp_socket m_tcp{};

private:
    Config m_cfg{};
    std::unique_ptr<::tls::Client> m_client{};
    ::tls::Client::ConnectionPtr m_conn{};
};

} // namespace everest::lib::io::tls
