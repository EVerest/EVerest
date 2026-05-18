// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
//
// Example: how to use udp_unconnected_client (an unconnected UDP datagram
// client, IPv4 or IPv6 auto-selected from the target). Sends a text message
// once per second to <dest>:<port> and prints any datagram received back,
// together with its source endpoint.
//
//   test_udp_unconnected_client <dest> <port> [iface]
//
// <dest> may be an IPv4 address, an IPv6 address, or a hostname. For IPv6
// link-local / multicast destinations pass the optional <iface> (used as the
// scope id and as a best-effort SO_BINDTODEVICE hint).

#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/timer_fd.hpp>
#include <everest/io/udp/endpoint.hpp>
#include <everest/io/udp/udp_payload.hpp>
#include <everest/io/udp/udp_unconnected_client.hpp>

using namespace everest::lib::io::udp;
using namespace everest::lib::io::event;
using namespace std::chrono_literals;

using generic_udp = typename udp_unconnected_client::interface;

namespace {

std::string to_text(udp_payload const& payload) {
    return std::string(payload.buffer.begin(), payload.buffer.end());
}

bool parse_args(int argc, char* argv[], std::string& dest, uint16_t& port, std::string& iface) {
    if (argc != 3 && argc != 4) {
        std::cout << "USAGE: test_udp_unconnected_client <dest> <port> [iface]\n";
        return false;
    }
    dest = argv[1];
    try {
        auto tmp = std::stoul(argv[2]);
        if (tmp > std::numeric_limits<uint16_t>::max()) {
            throw std::out_of_range("port");
        }
        port = static_cast<uint16_t>(tmp);
    } catch (...) {
        std::cout << "ERROR: '" << argv[2] << "' is not a valid port\n";
        return false;
    }
    iface = argc == 4 ? argv[3] : "";
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    std::string dest;
    std::string iface;
    uint16_t port = 0;
    if (not parse_args(argc, argv, dest, port, iface)) {
        return 1;
    }

    endpoint target;
    try {
        target = endpoint(dest, port, iface);
    } catch (std::exception const& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }

    udp_unconnected_client client(target, iface);

    client.set_error_handler([](int err, std::string const& msg) {
        if (err) {
            std::cerr << "ERROR (" << err << "): " << msg << "\n";
        }
    });
    client.set_rx_handler([&](udp_payload const& payload, generic_udp&) {
        std::cout << "RX (" << payload.size() << "): " << to_text(payload);
        if (const auto src = client.get_raw_handler()->last_source()) {
            std::cout << "  from [" << src->addr_str() << "]:" << src->port();
        }
        std::cout << "\n";
    });

    fd_event_handler ev;
    ev.register_event_handler(client.get_poll_fd(), [&](auto&) { client.sync(); }, {poll_events::read});

    timer_fd ticker;
    ticker.set_timeout(1s);
    int counter = 0;
    fd_event_handler::event_handler_type on_tick = [&](fd_event_handler::event_list const&) {
        ticker.set_timeout(1s); // re-arm
        std::ostringstream msg;
        msg << "ping #" << ++counter;
        udp_payload payload;
        payload.set_message(msg.str());
        std::cout << "TX: " << msg.str() << " -> [" << target.addr_str() << "]:" << target.port() << "\n";
        client.tx(payload);
    };
    ev.register_event_handler(&ticker, on_tick);

    std::cout << "udp unconnected client: sending to [" << target.addr_str() << "]:" << target.port()
              << (iface.empty() ? "" : (" via " + iface)) << " every 1s\n";
    while (true) {
        ev.poll();
    }
    return 0;
}
