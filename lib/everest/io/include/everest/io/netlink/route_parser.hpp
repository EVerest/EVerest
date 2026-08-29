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
 * @brief A link announcement (RTM_NEWLINK / RTM_DELLINK), reduced to the state a carrier watcher
 * needs.
 */
struct link_report {
    /// The interface index the message was about.
    int ifindex{0};
    /**
     * @brief The device name from IFLA_IFNAME.
     * @details Empty when the attribute was absent. Nothing the kernel emits today omits it, but a
     * report without a name must still be usable - see \ref link_tracker, which then falls back to
     * matching on an already known interface index.
     */
    std::string name;
    /**
     * @brief \c ifi_flags & \c IFF_LOWER_UP - the carrier signal.
     * @details This flag, and not \ref running or the operstate string, is the reliable carrier
     * indication for both TAP devices and physical netdevs, and substituting either of them is a
     * real bug rather than a stylistic choice - see \ref running and \ref is_carrier_up.
     */
    bool lower_up{false};
    /**
     * @brief \c ifi_flags & \c IFF_RUNNING - diagnostics only, never a carrier decision.
     * @details A TAP device created carrier-off is nevertheless announced once with IFF_RUNNING
     * set, because the operstate this flag reflects is corrected by the kernel's linkwatch work
     * only about a second later. A watcher keyed on IFF_RUNNING therefore sees a spurious ~1 s
     * carrier-up on every TAP creation or reset, so nothing may derive carrier from this flag.
     * It is decoded for diagnostics and for tests that assert the discrimination, nothing else.
     */
    bool running{false};
    /// \c ifi_flags & \c IFF_UP - the administrative flag, unrelated to carrier.
    bool admin_up{false};
    /// The message was RTM_DELLINK: the device is gone, which is a carrier-down for any consumer.
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
     * @details inet_ntop for AF_INET and AF_INET6, an upper-case hex string for any other family,
     * so an entry is always identifiable even when its address family is not understood. Empty when
     * the attribute was absent.
     */
    std::string address;
    /**
     * @brief NDA_LLADDR as an upper-case colon-separated MAC address, e.g. "0A:1B:2C:D3:E4:F5".
     * @details Empty unless the attribute carried exactly six bytes: a link layer address of any
     * other width is not a 48 bit MAC and is reported as absent rather than truncated.
     *
     * The format is fixed - upper case, colon separated, no shortening - because consumers
     * typically hand it straight to something that pattern-matches on it (an EVerest interface
     * variable, a token, an allowlist entry). Do not "normalise" it downstream.
     */
    std::string mac;
    /// The message was RTM_DELNEIGH: the entry is gone.
    bool deleted{false};
};

/**
 * @brief Everything one datagram read from a NETLINK_ROUTE socket contained.
 */
struct parse_result {
    /// Link announcements, in the order they appeared.
    std::vector<link_report> links;
    /// Neighbour announcements, in the order they appeared.
    std::vector<neighbor_report> neighbors;
    /// NLMSG_DONE was seen - the end of an RTM_GETLINK or RTM_GETNEIGH dump.
    bool dump_done{false};
    /// NLMSG_ERROR payload: 0 for an acknowledgement, otherwise the negative errno the kernel sent.
    int error{0};
    /**
     * @brief A message claimed to extend past the end of the buffer.
     * @details Parsing stopped at that message; everything decoded before it is still returned. A
     * consumer that needs a complete view should request a fresh dump.
     */
    bool truncated{false};
};

/**
 * @brief Decode one NETLINK_ROUTE datagram.
 * @details Pure: no syscalls and no state, and safe on arbitrary bytes - a malformed or hostile
 * buffer yields whatever decoded plus \ref everest::lib::io::netlink::parse_result::truncated,
 * never an out of bounds read.
 * Message types other than RTM_NEWLINK, RTM_DELLINK, RTM_NEWNEIGH, RTM_DELNEIGH, NLMSG_DONE and
 * NLMSG_ERROR are skipped.
 * @param[in] buffer Start of the datagram
 * @param[in] length Number of valid bytes in \p buffer
 * @return What the datagram contained
 */
parse_result parse(void const* buffer, std::size_t length);

/**
 * @brief Whether \p report describes a device that currently has carrier.
 * @details The documented consumer contract: link present and IFF_LOWER_UP set. A deleted device
 * never counts as up, whatever flags its RTM_DELLINK happened to carry.
 * @param[in] report The link announcement to judge
 * @return True if the device has carrier, false otherwise
 */
bool is_carrier_up(link_report const& report);

/**
 * @brief Whether a \c ndm_state means the neighbour is a live peer.
 * @details True for NUD_REACHABLE, NUD_STALE, NUD_DELAY, NUD_PROBE and NUD_PERMANENT. NUD_STALE is
 * included deliberately: it means "was reachable, not re-verified recently", not "gone". The kernel
 * only re-probes when something wants to send, so an idle but healthy peer legitimately stays
 * STALE indefinitely.
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
