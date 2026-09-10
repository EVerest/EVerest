// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <array>
#include <cstdint>
#include <everest/io/event/unique_fd.hpp>
#include <everest/io/mdns/mdns.hpp>
#include <everest/io/udp/endpoint.hpp>
#include <everest/io/udp/udp_payload.hpp>
#include <everest/io/udp/udp_socket.hpp>
#include <functional>
#include <optional>
#include <string>
#include <sys/socket.h>

namespace everest::lib::io::mdns {

class mdns_socket : public udp::udp_socket_base {
public:
    using PayloadT = udp::udp_payload;

    mdns_socket() = default;
    ~mdns_socket() = default;

    /// Open an mDNS socket on the given interface. \p family selects the
    /// transport: AF_INET (224.0.0.251, default) or AF_INET6 (ff02::fb).
    /// Returns false when the family is not available on the interface.
    bool open(std::string const& interface, int family = AF_INET);
    bool tx(udp::udp_payload const& payload);
    bool rx(udp::udp_payload& payload);

    bool query(std::string const& what);

    /// Send an mDNS response advertising the given service
    bool announce(mDNS_discovery const& service, std::string const& service_type);

private:
    std::array<uint8_t, udp::udp_payload::max_size> rx_buffer;
    udp::endpoint m_target;
};

} // namespace everest::lib::io::mdns
