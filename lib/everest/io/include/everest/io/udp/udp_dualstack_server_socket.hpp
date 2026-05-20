// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include <everest/io/event/unique_fd.hpp>
#include <everest/io/udp/endpoint.hpp>
#include <everest/io/udp/udp_payload.hpp>

namespace everest::lib::io::udp {

/**
 * @class udp_dualstack_server_socket
 * @brief A dual-stack (IPv6 + IPv4-mapped) unconnected UDP *server* socket.
 *
 * Binds [::]:port via socket::open_udp_dualstack_server_socket, records the
 * last datagram's source, and replies to it. Synchronous ClientPolicy of
 * event::fd_event_client.
 */
class udp_dualstack_server_socket {
public:
    using PayloadT = udp_payload;

    udp_dualstack_server_socket() = default;

    /**
     * @brief Open the server. Never throws (sync policy).
     * @param[in] port Local UDP port; 0 picks an ephemeral port.
     * @param[in] device Optional interface name. Empty = no binding.
     * @return True on success.
     */
    bool open(std::uint16_t port, std::string device = {});

    /**
     * @brief Reply to the most recent source.
     * @return False if nothing has been received yet.
     */
    bool tx(udp_payload const& payload);

    /**
     * @brief Receive a datagram; records last_source().
     */
    bool rx(udp_payload& payload);

    int get_fd() const;
    int get_error() const;

    std::optional<endpoint> last_source() const {
        return m_last_source;
    }

private:
    event::unique_fd m_owned_udp_fd;
    std::optional<endpoint> m_last_source;
    std::array<uint8_t, udp_payload::max_size> m_rx_buffer;
};

} // namespace everest::lib::io::udp
