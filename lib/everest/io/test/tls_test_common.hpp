// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Shared helpers for the everest_io_tls_test binary. All four TLS test TUs
// compile into this one executable, so everything here is inline/header-only.
//
// The PKI fixture files (server_chain.pem, server_priv.pem, ...) are resolved
// relative to the working directory, which ctest pins to
// build/lib/everest/tls/tests — always run this binary via ctest.
//
// Naming note: inside everest::lib::io::test an unqualified `tls::` finds the
// sibling everest::lib::io::tls namespace; the libtls types are reached via the
// fully qualified `::tls::`.

#pragma once

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/tls/tls_client_socket.hpp>
#include <everest/io/tls/tls_listener.hpp>
#include <everest/io/tls/tls_server.hpp>
#include <everest/io/tls/tls_server_socket.hpp>
#include <everest/tls/tls.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace everest::lib::io::test {

/// Server-side chain/cipher core matching the test PKI: TLS 1.2 with the
/// ECDSA test chain, TLS 1.3 disabled (empty ciphersuites), no client
/// verification. Callers add transport specifics (host/service/socket for a
/// raw ::tls::Server, or wrap via listener_test_config()).
inline ::tls::Server::config_t server_test_config() {
    ::tls::Server::config_t scfg;
    scfg.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
    scfg.ciphersuites = "";
    auto& chain = scfg.chains.emplace_back();
    chain.certificate_chain_file = "server_chain.pem";
    chain.private_key_file = "server_priv.pem";
    chain.trust_anchor_file = "server_root_cert.pem";
    chain.ocsp_response_files = {"ocsp_response.der", "ocsp_response.der"};
    scfg.verify_client = false;
    scfg.io_timeout_ms = 1000;
    return scfg;
}

/// tls_listener::Config on the test PKI with an ephemeral port on bind_addr.
inline tls::tls_listener::Config listener_test_config(std::string bind_addr = "127.0.0.1", bool ipv6_only = false) {
    tls::tls_listener::Config lcfg;
    lcfg.tls = server_test_config();
    lcfg.bind_addr = std::move(bind_addr);
    lcfg.bind_port = 0;
    lcfg.ipv6_only = ipv6_only;
    return lcfg;
}

/// Client config trusting the test server root so chain validation passes.
/// TLS 1.3 is disabled (empty ciphersuites) to match server_test_config().
/// Hostname verification is left off; tests opt in via verify_subject_name.
inline tls::tls_client_socket::Config client_test_config(std::int32_t io_timeout_ms = 2000,
                                                         std::string host_for_sni = {}) {
    tls::tls_client_socket::Config cfg;
    cfg.tls.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
    cfg.tls.ciphersuites = "";
    cfg.tls.verify_locations_file = "server_root_cert.pem";
    cfg.tls.io_timeout_ms = io_timeout_ms;
    cfg.tls.verify_server = true;
    cfg.host_for_sni = std::move(host_for_sni);
    return cfg;
}

/// Drive the event loop — poll(50ms) + run_actions() per tick — until done()
/// returns true or the budget expires. The timed poll guarantees the deadline
/// is re-checked every tick even with no events, so a stall fails the test
/// deterministically instead of hanging. Returns whether done() was satisfied.
template <class Predicate>
inline bool pump_until(event::fd_event_handler& ev, Predicate&& done, std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    bool satisfied = done();
    while (!satisfied && std::chrono::steady_clock::now() < deadline) {
        ev.poll(std::chrono::milliseconds(50));
        ev.run_actions();
        satisfied = done();
    }
    return satisfied;
}

/// A tls_listener on the test PKI whose accept callback echoes every received
/// payload back to the peer. The accepted tls_server is registered on the
/// given event handler and owned by server_conn (it must outlive the accept
/// callback; tests drop it explicitly to simulate a server-side close). Only
/// a single accepted connection is kept — enough for these tests.
/// Non-movable (the accept callback captures `this`); obtain instances via
/// make_echo_listener(), which relies on guaranteed copy elision.
struct echo_listener {
    explicit echo_listener(event::fd_event_handler& ev, tls::tls_listener::Config cfg = listener_test_config()) :
        listener(std::move(cfg)) {
        listener.set_accept_callback(
            [this, &ev](std::unique_ptr<tls::tls_server> srv, std::string /*ip*/, std::uint16_t /*peer_port*/) {
                srv->set_rx_handler([](tls::tls_server_socket::PayloadT const& payload, auto& self) {
                    self.tx(payload); // echo
                });
                ev.register_event_handler(srv.get());
                server_conn = std::move(srv);
            });
        registered = ev.register_event_handler(&listener);
    }

    std::uint16_t port() const {
        return listener.listen_port();
    }

    tls::tls_listener listener;
    std::unique_ptr<tls::tls_server> server_conn;
    bool registered{false};
};

inline echo_listener make_echo_listener(event::fd_event_handler& ev,
                                        tls::tls_listener::Config cfg = listener_test_config()) {
    return echo_listener(ev, std::move(cfg));
}

/// Blocking libtls client used as the remote peer on a worker thread while the
/// main thread drives the event loop: connect, TLS handshake, write payload,
/// read it echoed back, verify. Reports through client_ok / client_error.
/// ciphersuites stays at the library default here (a TLS 1.3 offer is fine:
/// the test servers disable TLS 1.3, so TLS 1.2 is negotiated either way).
inline void run_blocking_echo_client(std::string const& host, std::uint16_t port,
                                     std::vector<std::uint8_t> const& payload, std::atomic<bool>& client_ok,
                                     std::string& client_error) {
    ::tls::Client client;
    ::tls::Client::config_t ccfg;
    ccfg.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
    ccfg.verify_locations_file = "server_root_cert.pem";
    ccfg.io_timeout_ms = 2000;
    ccfg.verify_server = true;
    if (!client.init(ccfg)) {
        client_error = "client.init failed";
        return;
    }

    const std::string port_str = std::to_string(port);
    auto conn = client.connect(host.c_str(), port_str.c_str(), false, 2000);
    if (!conn) {
        client_error = "client.connect returned nullptr";
        return;
    }
    if (conn->connect() != ::tls::Connection::result_t::success) {
        client_error = "TLS handshake failed on client side";
        return;
    }

    std::size_t written = 0;
    if (conn->write(reinterpret_cast<const std::byte*>(payload.data()), payload.size(), written, 2000) !=
            ::tls::Connection::result_t::success ||
        written != payload.size()) {
        client_error = "client write failed";
        return;
    }

    std::vector<std::uint8_t> echo;
    echo.reserve(payload.size());
    while (echo.size() < payload.size()) {
        std::byte buf[8192]{};
        std::size_t nread = 0;
        if (conn->read(buf, sizeof buf, nread, 2000) != ::tls::Connection::result_t::success) {
            client_error = "client read failed";
            return;
        }
        echo.insert(echo.end(), reinterpret_cast<std::uint8_t*>(buf), reinterpret_cast<std::uint8_t*>(buf) + nread);
    }

    if (echo != payload) {
        client_error = "client echo mismatch";
        return;
    }

    conn->shutdown();
    client_ok = true;
}

} // namespace everest::lib::io::test
