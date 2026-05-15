// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#pragma once
#include <everest/io/event/fd_event_client.hpp>
#include <everest/io/tls/tls_client_socket.hpp>

namespace everest::lib::io::tls {

/**
 * @brief Event-loop-driven TLS client over fd_event_client.
 *
 * Wraps tls_client_socket in the generic fd_event_client machinery. All four
 * ctor arguments are forwarded to tls_client_socket::setup(cfg, host, port,
 * timeout_ms) so the TLS configuration is supplied up front; no separate
 * configuration step is needed.
 *
 * The TCP connect + TLS handshake do not start at construction. They are
 * dispatched on the first sync() / poll() of the fd_event_handler the client
 * is registered with.
 *
 * @code
 *   tls_client_socket::Config cfg;
 *   cfg.tls.verify_locations_file = "server_root_cert.pem";
 *   cfg.host_for_sni = "example.com";
 *
 *   tls_client client(cfg, "192.0.2.1", std::uint16_t{443}, 5000);
 *   client.set_rx_handler([](auto const& payload, auto& dev) { ... });
 *   client.set_on_ready_action([&client] { client.tx({...}); });
 *
 *   fd_event_handler ev;
 *   ev.register_event_handler(&client);
 *   std::atomic_bool running{true};
 *   ev.run(running);
 * @endcode
 */
using tls_client = event::fd_event_client<tls_client_socket>::type;

} // namespace everest::lib::io::tls
