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
 * @brief Decides when the peer on a point-to-point link counts as gone, from the kernel's
 * neighbour table.
 *
 * @details Wraps a \ref neighbor_table - which reports facts and draws no conclusions - with the
 * judgement a link supervisor needs: is the peer still answering, and if not, has it been long
 * enough to say so. The waiting itself belongs to the caller, which owns the timer; this class says
 * when to start and stop it and answers the question when it expires.
 *
 * Why neighbour discovery is worth this at all: on a link whose PHY reports only its own state -
 * 10BASE-T1S with autonegotiation off, for instance, where the coordinator's PLCA status asserts
 * whether or not a node answers - a carrier that says "my PHY is operational" can outlive the peer.
 * Neighbour discovery actively probes, so its NUD state machine notices a peer that stopped
 * answering, at no cost and on any netdev.
 *
 * The judgement is deliberately conservative, because a false positive tears down a working
 * session:
 * - It only ever speaks when it has seen at least one neighbour. An empty table is <b>no
 *   opinion</b>, never "the peer is gone" - that is the state before the peer has sent anything,
 *   and also the state after the kernel garbage-collected an idle entry. Removal of the last entry
 *   is therefore not a loss - with one exception: the kernel also garbage collects NUD_FAILED
 *   entries, within seconds of the failure, and a removal that carries NUD_FAILED is that dead
 *   peer being tidied away, not an idle one. It leaves the verdict where the failure put it, so a
 *   grace period already running keeps running and \ref peer_is_lost stays true.
 * - A single NUD_FAILED is not enough. It only counts while no other neighbour of the device is
 *   alive, and even then only if none recovers within the caller's grace period.
 * - The grace period is not restarted by further failures - that would push the deadline out
 *   exactly when it should be expiring.
 * - NUD_STALE, NUD_DELAY, NUD_PROBE and NUD_PERMANENT all count as alive (see
 *   \ref is_neighbor_alive). The kernel only re-probes when something wants to send, so an idle but
 *   healthy peer legitimately sits in STALE indefinitely.
 *
 * Feed one instance per device, with the neighbour reports of that device only -
 * \ref link_tracker::ifindex is the usual filter. Pure: no socket, no syscalls, no timer. Not
 * synchronized; feed it from one thread.
 */
class peer_liveness {
public:
    /// What the caller should do with its grace timer, plus an address worth reporting upward.
    struct verdict {
        /**
         * @brief Start the grace timer, or leave it running: the device has neighbours and none of
         * them is alive any more.
         * @details Never set together with \ref cancel_grace. Both unset means "no change" - keep
         * doing whatever you were doing.
         */
        bool arm_grace{false};
        /// Disarm the grace timer: a neighbour is alive again, or there is nothing to judge.
        bool cancel_grace{false};
        /**
         * @brief Non-empty when a neighbour just became NUD_REACHABLE and carried a link layer
         * address.
         * @details The peer's MAC address, for a consumer that wants to report or match on it. See
         * \ref neighbor_report::mac for the format and why it is fixed.
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
     * @brief Whether the peer counts as gone. Ask this when the grace period expires.
     * @details True when the device had neighbours and none of them is alive, or when its last
     * neighbour was removed by the kernel while NUD_FAILED (see the class description).
     *
     * Entries that are neither alive nor failed - NUD_INCOMPLETE, resolution still in flight -
     * count towards a lost peer here, even though \ref apply refuses to *start* a grace period for
     * them. That asymmetry is deliberate and safe: a grace period is only ever armed by a
     * NUD_FAILED, so reaching its expiry means a definitive failure was already seen and the
     * re-resolution it triggered has not succeeded either. An address still merely INCOMPLETE a
     * grace period after failing is not evidence of a live peer.
     * @return True if the peer counts as gone, false otherwise
     */
    bool peer_is_lost() const;

    /**
     * @brief Forget everything.
     * @details Appropriate whenever what the table holds stops describing the current link - the
     * carrier dropping, the device disappearing, the session ending or being suspended. The kernel
     * does not reliably announce the removal of entries in those cases.
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
     * @details The table itself cannot hold this: the entry is gone. Set by \ref apply on such a
     * removal, cleared by any live neighbour, by a removal that is not a failure, and by \ref clear.
     */
    bool m_last_entry_failed{false};
};

} // namespace everest::lib::io::netlink
