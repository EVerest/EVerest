// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace everest::lib::io::netlink {

/**
 * @brief A link announcement (RTM_NEWLINK / RTM_DELLINK), reduced to what a carrier watcher needs.
 */
struct link_report {
    /// The interface index the message was about.
    int ifindex{0};
    /**
     * @brief The device name from IFLA_IFNAME.
     * @details Empty when the attribute was absent; \ref link_tracker then matches on a known index.
     */
    std::string name;
    /**
     * @brief \c ifi_flags & \c IFF_LOWER_UP: the carrier signal.
     * @details The only reliable carrier indication for TAP and physical netdevs; see \ref running, \ref is_carrier_up.
     */
    bool lower_up{false};
    /**
     * @brief \c ifi_flags & \c IFF_RUNNING: diagnostics only, never a carrier decision.
     * @details A TAP created carrier-off is announced once with IFF_RUNNING set; linkwatch corrects the
     * operstate about a second later, so keying on it gives a spurious ~1 s carrier-up per TAP creation/reset.
     */
    bool running{false};
    /// \c ifi_flags & \c IFF_UP: the administrative flag, unrelated to carrier.
    bool admin_up{false};
    /// The message was RTM_DELLINK: the device is gone, a carrier-down for any consumer.
    bool deleted{false};
};

/**
 * @brief A neighbour table announcement (RTM_NEWNEIGH / RTM_DELNEIGH).
 */
struct neighbor_report {
    /// The interface index the entry belongs to.
    int ifindex{0};
    /// \c ndm_state, a bitmask of \c NUD_*. See \ref is_neighbor_alive and \ref is_neighbor_failed.
    std::uint16_t nud_state{0};
    /**
     * @brief NDA_DST rendered printable.
     * @details inet_ntop for AF_INET and AF_INET6, upper-case hex for other families; empty when absent.
     */
    std::string address;
    /**
     * @brief NDA_LLADDR as an upper-case colon-separated MAC address, e.g. "0A:1B:2C:D3:E4:F5".
     * @details Empty unless exactly six bytes. Fixed format (consumers pattern-match on it); do not normalise it.
     */
    std::string mac;
    /// The message was RTM_DELNEIGH: the entry is gone.
    bool deleted{false};
};

/**
 * @brief Everything one datagram read from a NETLINK_ROUTE socket contained.
 */
struct parse_result {
    /// Link announcements, in order.
    std::vector<link_report> links;
    /// Neighbour announcements, in order.
    std::vector<neighbor_report> neighbors;
    /// NLMSG_DONE was seen: the end of an RTM_GETLINK or RTM_GETNEIGH dump.
    bool dump_done{false};
    /// NLMSG_ERROR payload: 0 for an acknowledgement, otherwise the negative errno the kernel sent.
    int error{0};
    /**
     * @brief A message claimed to extend past the end of the buffer.
     * @details Parsing stopped there; what was decoded before is returned. Request a fresh dump for a full view.
     */
    bool truncated{false};
};

/**
 * @brief Decode one NETLINK_ROUTE datagram.
 * @details Pure and safe on arbitrary bytes: a malformed buffer yields what was decoded plus
 * \ref everest::lib::io::netlink::parse_result::truncated, never an out of bounds read. Message types
 * other than RTM_NEWLINK, RTM_DELLINK, RTM_NEWNEIGH, RTM_DELNEIGH, NLMSG_DONE and NLMSG_ERROR are skipped.
 * @param[in] buffer Start of the datagram
 * @param[in] length Number of valid bytes in \p buffer
 * @return What the datagram contained
 */
parse_result parse(void const* buffer, std::size_t length);

/**
 * @brief Whether \p report describes a device that currently has carrier.
 * @details Link present and IFF_LOWER_UP set. A deleted device never counts as up, whatever flags it carried.
 * @param[in] report The link announcement to judge
 * @return True if the device has carrier, false otherwise
 */
bool is_carrier_up(link_report const& report);

/**
 * @brief Whether a \c ndm_state means the neighbour is a live peer.
 * @details True for NUD_REACHABLE, NUD_STALE, NUD_DELAY, NUD_PROBE and NUD_PERMANENT. NUD_STALE means
 * "was reachable, not re-verified"; the kernel only re-probes on send, so an idle peer stays STALE indefinitely.
 * @param[in] nud_state The \c ndm_state to judge
 * @return True if the neighbour counts as alive, false otherwise
 */
bool is_neighbor_alive(std::uint16_t nud_state);

/**
 * @brief Whether a \c ndm_state means address resolution gave up on the neighbour (NUD_FAILED).
 * @param[in] nud_state The \c ndm_state to judge
 * @return True if the neighbour has failed, false otherwise
 */
bool is_neighbor_failed(std::uint16_t nud_state);

} // namespace everest::lib::io::netlink
