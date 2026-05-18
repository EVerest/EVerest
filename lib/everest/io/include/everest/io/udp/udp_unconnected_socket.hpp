// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <array>
#include <optional>
#include <string>

#include <everest/io/event/unique_fd.hpp>
#include <everest/io/udp/endpoint.hpp>
#include <everest/io/udp/udp_payload.hpp>

namespace everest::lib::io::udp {

/**
 * An unconnected UDP datagram socket (IPv4 or IPv6, selected automatically from
 * the target \ref endpoint). It sends to a fixed destination configured at
 * \ref open and receives datagrams from any source (no ::connect, no multicast
 * group join), recording the last sender. Designed to be used as the
 * \p ClientPolicy of \ref event::fd_event_client (synchronous variant).
 */
class udp_unconnected_socket {
public:
    /**
     * @var PayloadT
     * @brief Type of the payload for TX and RX operations
     */
    using PayloadT = udp_payload;

    /**
     * @brief The class is default constructed
     */
    udp_unconnected_socket() = default;

    /**
     * @brief Open the socket and set the fixed send destination.
     * @details Implementation for \p ClientPolicy (sync variant). Never throws.
     * @param[in] target Destination for \ref tx; selects the socket family.
     * @param[in] iface Optional interface name. Empty uses @p target's hint.
     * @return True on success, false otherwise.
     */
    bool open(endpoint target, std::string iface = {});

    /**
     * @brief Send a \ref udp_payload to the configured destination.
     * @details Implementation for \p ClientPolicy
     * @param[in] payload Payload
     * @return True on success, false otherwise.
     */
    bool tx(udp_payload const& payload);

    /**
     * @brief Receive a \ref udp_payload from the socket.
     * @details Implementation for \p ClientPolicy. On success the sender
     * endpoint is recorded and retrievable via \ref last_source.
     * @param[out] payload Payload
     * @return True on success, false otherwise.
     */
    bool rx(udp_payload& payload);

    /**
     * @brief Get the file descriptor of the socket
     * @details Implementation for \p ClientPolicy
     * @return The file descriptor of the socket
     */
    int get_fd() const;

    /**
     * @brief Get pending errors on the socket.
     * @details Implementation for \p ClientPolicy
     * @return The current errno of the socket. Zero with no pending error.
     */
    int get_error() const;

    /**
     * @brief Source endpoint of the most recently received datagram.
     * @return The endpoint, or std::nullopt if nothing was received yet.
     */
    std::optional<endpoint> last_source() const {
        return m_last_source;
    }

private:
    event::unique_fd m_owned_udp_fd;
    endpoint m_target;
    std::optional<endpoint> m_last_source;
    std::array<uint8_t, udp_payload::max_size> m_rx_buffer;
};

} // namespace everest::lib::io::udp
