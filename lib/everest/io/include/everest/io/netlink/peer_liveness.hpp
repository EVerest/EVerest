// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <everest/io/netlink/neighbor_table.hpp>
#include <everest/io/netlink/route_parser.hpp>

#include <cstddef>
#include <string>

namespace everest::lib::io::netlink {

/**
 * @brief Decides from the kernel's neighbour table when the peer on a point-to-point link counts as gone.
 * @details Wraps a \ref neighbor_table with the judgement a link supervisor needs; the caller owns the
 * grace timer, this class says when to arm and cancel it and answers \ref peer_is_lost on expiry.
 * Neighbour discovery actively probes, so it notices a peer that stopped answering while the PHY still
 * reports carrier (10BASE-T1S with autonegotiation off: PLCA status reflects only the local node).
 * Conservative, since a false positive tears down a working session:
 * - An empty table is no opinion (the state before the peer sent anything, and after the kernel garbage
 *   collected an idle entry). Exception: a removal carrying NUD_FAILED (the kernel garbage collects failed
 *   entries within seconds) leaves the verdict in place, so a running grace period continues.
 * - A NUD_FAILED counts only while no other neighbour is alive, and only if none recovers within grace.
 * - Further failures do not restart the grace period.
 * - NUD_STALE, NUD_DELAY, NUD_PROBE and NUD_PERMANENT count as alive (\ref is_neighbor_alive); the kernel
 *   only re-probes when something wants to send, so an idle peer sits in STALE indefinitely.
 * One instance per device, fed that device's reports only (\ref link_tracker::ifindex). Pure: no socket,
 * no syscalls, no timer. Not synchronized; feed it from one thread.
 */
class peer_liveness {
public:
    /// What the caller should do with its grace timer, plus an address worth reporting upward.
    struct verdict {
        /**
         * @brief Arm the grace timer or leave it running: the device has neighbours and none is alive.
         * @details Never set together with \ref cancel_grace. Both unset means no change.
         */
        bool arm_grace{false};
        /// Disarm the grace timer: a neighbour is alive again, or there is nothing to judge.
        bool cancel_grace{false};
        /**
         * @brief The peer's MAC when a neighbour just became NUD_REACHABLE with a link layer address.
         * @details Empty otherwise. Format as in \ref neighbor_report::mac.
         */
        std::string reachable_mac{};
    };

    /**
     * @brief Fold one neighbour report of the watched device into the table and judge it.
     * @param[in] report The announcement, as decoded by \ref parse
     * @return What the caller should do about it
     */
    verdict apply(neighbor_report const& report);

    /**
     * @brief Whether the peer counts as gone. Ask when the grace period expires.
     * @details True when the device had neighbours and none is alive, or when its last neighbour was
     * removed while NUD_FAILED. NUD_INCOMPLETE counts as lost here although \ref apply arms no grace
     * period for it: grace is only armed by a NUD_FAILED, so at expiry re-resolution has not succeeded.
     * @return True if the peer counts as gone, false otherwise
     */
    bool peer_is_lost() const;

    /**
     * @brief Forget everything.
     * @details Use when the table stops describing the link (carrier drop, device gone, session ended
     * or suspended); the kernel does not reliably announce removals then.
     */
    void clear();

    bool empty() const;
    std::size_t size() const;
    /// Whether any tracked neighbour is in a state that counts as alive.
    bool any_alive() const;

private:
    neighbor_table m_table;
    /**
     * @brief The table became empty because its last entry was removed while NUD_FAILED.
     * @details Set by \ref apply on such a removal; cleared by a live neighbour, a non-failure removal, or \ref clear.
     */
    bool m_last_entry_failed{false};
};

} // namespace everest::lib::io::netlink
