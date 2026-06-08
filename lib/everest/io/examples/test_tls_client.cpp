// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/timer_fd.hpp>
#include <everest/io/tls/tls_client.hpp>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

using namespace everest::lib::io::tls;
using namespace everest::lib::io::event;

using payload_t = tls_client::PayloadT;
using tls_endpoint = tls_endpoint_base<tls_client_socket>;

int main(int argc, char* argv[]) {
    std::cout << "TLS echo client example" << std::endl;

    std::string host = "127.0.0.1";
    std::uint16_t port = 8443;
    std::string trust_anchor = "server_root_cert.pem";

    if (argc == 4) {
        host = argv[1];
        try {
            auto tmp = std::stoul(argv[2]);
            if (tmp > 65535u) {
                throw std::invalid_argument("port out of range");
            }
            port = static_cast<std::uint16_t>(tmp);
        } catch (...) {
            std::cerr << "ERROR: invalid port '" << argv[2] << "'" << std::endl;
            return 1;
        }
        trust_anchor = argv[3];
    } else if (argc != 1) {
        std::cout << "USAGE: test_tls_client [host port trust_anchor.pem]" << std::endl;
        std::cout << "  Defaults: 127.0.0.1 8443 server_root_cert.pem" << std::endl;
        return 1;
    }

    tls_client_socket::Config cfg{};
    cfg.tls.cipher_list = "ECDHE-ECDSA-AES128-SHA256";
    cfg.tls.ciphersuites = "";
    cfg.tls.verify_locations_file = trust_anchor.c_str();
    cfg.tls.io_timeout_ms = 5000;
    cfg.tls.verify_server = true;
    cfg.host_for_sni = host;

    std::cout << "Connecting to " << host << ":" << port << " ..." << std::endl;

    fd_event_handler ev_handler;
    tls_client client(cfg, host, port, 5000);

    std::atomic<bool> running{true};
    int replies_received = 0;
    const int expected_replies = 3;

    client.set_error_handler([&running](int err, std::string const& msg) {
        if (err != 0) {
            std::cerr << "ERROR (" << err << "): " << msg << std::endl;
            running = false;
        }
    });

    client.set_rx_handler([&replies_received, &running, expected_replies](payload_t const& payload, tls_endpoint&) {
        ++replies_received;
        std::cout << "Reply (" << payload.size() << " bytes): " << std::string(payload.begin(), payload.end());
        if (replies_received >= expected_replies) {
            running = false;
        }
    });

    client.set_on_ready_action([&client]() {
        std::cout << "Handshake complete. Sending messages..." << std::endl;
        for (const char* text : {"hello tls\n", "second message\n", "goodbye\n"}) {
            payload_t msg(text, text + std::strlen(text));
            client.tx(msg);
        }
    });

    ev_handler.register_event_handler(&client);
    ev_handler.run(running);

    // Unregister before the client is destroyed. Required whenever the
    // fd_event_handler outlives the client; safe (and recommended) here too.
    ev_handler.unregister_event_handler(&client);

    std::cout << "Done." << std::endl;
    return 0;
}
