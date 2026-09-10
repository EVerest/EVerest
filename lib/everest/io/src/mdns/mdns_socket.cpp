// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "everest/io/udp/udp_payload.hpp"
#include "everest/io/udp/udp_socket.hpp"
#include <arpa/inet.h>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/unique_fd.hpp>
#include <everest/io/mdns/mdns.hpp>
#include <everest/io/mdns/mdns_socket.hpp>
#include <everest/io/socket/socket.hpp>
#include <iostream>
#include <net/if.h>
#include <netinet/in.h>
#include <optional>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace everest::lib::io::mdns {

/////////////////////////////////////////////////

bool mdns_socket::open(std::string const& interface, int family) {
    // "family not available on this interface" is an expected condition with
    // dual-stack discovery, so socket setup failures map to a false return.
    int error = 0;
    try {
        if (family == AF_INET6) {
            auto socket = socket::open_mdns_socket6(interface);
            adopt(std::move(socket));
            // the endpoint resolves the interface to sin6_scope_id, required
            // to send to the link-scoped group
            m_target = udp::endpoint(mdns_multicast_ipv6, mdns_port, interface);
        } else {
            auto socket = socket::open_mdns_socket(interface);
            adopt(std::move(socket));
            m_target = udp::endpoint(mdns_multicast_ipv4, mdns_port);
        }

        // SO_ERROR is read-and-clear. The pending error is read once and kept, so a
        // false return still carries the reason instead of a value already consumed.
        error = socket::get_pending_error(get_fd());
        if (error == 0) {
            return true;
        }
    } catch (socket::socket_error const& e) {
        error = e.error();
    } catch (...) {
        // An unusable interface fails the address lookup, which carries no errno.
        // get_error() then falls through to probing an unassigned descriptor, which
        // is nonzero, so the client still resets.
    }
    record_connect_failure(error);
    return false;
}

bool mdns_socket::tx(PayloadT const& payload) {
    return tx_impl(payload.buffer.data(), payload.size(), m_target);
}

bool mdns_socket::rx(PayloadT& payload) {
    ssize_t msg_size = 0;
    // Note: rx_impl uses a sockaddr_in-sized peer buffer; on an AF_INET6 socket
    // the peer address gets truncated while the payload stays intact. The peer
    // udp_info is discarded here and must not be trusted for v6 mdns sockets.
    auto result = rx_impl(rx_buffer.data(), rx_buffer.size(), msg_size);
    if (result) {
        payload.set_message(rx_buffer.data(), msg_size);
    }
    return result.has_value();
}

bool mdns_socket::query(std::string const& what) {
    PayloadT payload;
    payload.buffer = everest::lib::io::mdns::create_mdns_query(what);
    return tx(payload);
}

bool mdns_socket::announce(mDNS_discovery const& service, std::string const& service_type) {
    PayloadT payload;
    payload.buffer = create_mdns_response(service, service_type);
    return tx(payload);
}

} // namespace everest::lib::io::mdns
