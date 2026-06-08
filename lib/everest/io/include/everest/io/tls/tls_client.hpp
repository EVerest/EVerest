// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#pragma once
#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/tls/tls_client_socket.hpp>
#include <everest/io/tls/tls_endpoint_base.hpp>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace everest::lib::io::tls {

// Event-loop-driven TLS client: registering it with an fd_event_handler is all
// that is needed to drive it. The TCP connect runs on an owned worker thread
// joined on teardown; the TLS handshake is then driven on the loop (the blocking
// open() is not used).
// The ctor args are forwarded to tls_client_socket::setup(cfg, host, port, timeout_ms).
class tls_client : public tls_endpoint_base<tls_client_socket> {
public:
    tls_client(tls_client_socket::Config cfg, std::string host, std::uint16_t port, int timeout_ms);

    tls_client(tls_client const&) = delete;
    tls_client(tls_client&&) = delete;
    tls_client& operator=(tls_client const&) = delete;
    tls_client& operator=(tls_client&&) = delete;
    ~tls_client() override;

private:
    void start(event::fd_event_handler& handler) override;
    void stop() override;

    event::event_fd m_connected;
    // Written by the connect worker, read by the m_connected handler on the loop
    // thread. Atomic: the eventfd wake is not a happens-before for this.
    std::atomic<bool> m_connect_ok{false};
    // Owns the TCP connect worker; joined on teardown (stop()/dtor) before the
    // socket is closed so the worker cannot touch a torn-down m_socket.
    std::thread m_worker;

    tls_client_socket::Config m_cfg;
    std::string m_host;
    std::uint16_t m_port;
    int m_timeout_ms;
};

} // namespace everest::lib::io::tls
