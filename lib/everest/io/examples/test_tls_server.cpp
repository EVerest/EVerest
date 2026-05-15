// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/tls/tls_listener.hpp>
#include <everest/io/tls/tls_server_connection.hpp>

#include <atomic>
#include <cstdint>
#include <deque>
#include <iostream>
#include <memory>
#include <string>

using namespace everest::lib::io::tls;
using namespace everest::lib::io::event;

int main(int argc, char* argv[]) {
    std::cout << "TLS echo server example" << std::endl;

    std::uint16_t bind_port = 8443;
    std::string cert_chain = "server_chain.pem";
    std::string private_key = "server_priv.pem";
    std::string trust_anchor = "server_root_cert.pem";

    if (argc == 5) {
        try {
            auto tmp = std::stoul(argv[1]);
            if (tmp > 65535u) {
                throw std::invalid_argument("port out of range");
            }
            bind_port = static_cast<std::uint16_t>(tmp);
        } catch (...) {
            std::cerr << "ERROR: invalid port '" << argv[1] << "'" << std::endl;
            return 1;
        }
        cert_chain = argv[2];
        private_key = argv[3];
        trust_anchor = argv[4];
    } else if (argc != 1) {
        std::cout << "USAGE: test_tls_server [port cert_chain.pem key.pem trust_anchor.pem]" << std::endl;
        std::cout << "  Defaults: 8443 server_chain.pem server_priv.pem server_root_cert.pem" << std::endl;
        return 1;
    }

    tls_listener::Config lcfg;
    auto& chain = lcfg.tls.chains.emplace_back();
    chain.certificate_chain_file = cert_chain.c_str();
    chain.private_key_file = private_key.c_str();
    chain.trust_anchor_file = trust_anchor.c_str();
    lcfg.tls.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
    lcfg.tls.ciphersuites = "";
    lcfg.tls.verify_client = false;
    lcfg.tls.io_timeout_ms = 1000;
    lcfg.bind_addr = "127.0.0.1";
    lcfg.bind_port = bind_port;

    fd_event_handler ev_handler;
    tls_listener listener(std::move(lcfg));

    std::cout << "Listener will accept on 127.0.0.1:" << listener.listen_port() << " once the event loop runs"
              << std::endl;

    // Keep accepted connections alive for the lifetime of the loop. Echo the
    // first received chunk back, then close the connection to demonstrate
    // close_and_unregister().
    std::deque<std::shared_ptr<tls_server_connection>> connections;

    listener.set_accept_callback([&ev_handler, &connections](std::unique_ptr<tls_server_connection> conn,
                                                             std::string const& peer_ip, std::uint16_t peer_port) {
        std::cout << "Accepted from " << peer_ip << ":" << peer_port << std::endl;
        std::shared_ptr<tls_server_connection> sp(std::move(conn));
        sp->set_rx_handler([](tls_server_connection::PayloadT const& payload, tls_server_connection& self) {
            std::cout << "RX (" << payload.size() << " bytes): " << std::string(payload.begin(), payload.end())
                      << std::flush;
            self.tx(payload); // echo
        });
        sp->set_close_handler([](tls_server_connection&) { std::cout << "Connection closed" << std::endl; });
        ev_handler.register_event_handler(sp.get());
        connections.push_back(std::move(sp));
    });

    ev_handler.register_event_handler(&listener);

    std::atomic<bool> running{true};
    ev_handler.run(running);

    // Issue a TLS shutdown and unregister each connection from the event
    // handler before the outer scope tears them down. This is the recommended
    // shutdown pattern: it sends a TLS close_notify to the peer so the peer
    // does not log a truncation error, and it removes the connection from the
    // handler in a deterministic order.
    for (auto& sp : connections) {
        sp->close_and_unregister();
    }
    return 0;
}
