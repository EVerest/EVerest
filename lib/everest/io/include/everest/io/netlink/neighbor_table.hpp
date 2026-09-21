// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <everest/io/netlink/route_parser.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>

namespace everest::lib::io::netlink {

/**
 * @brief Mirror of the kernel's neighbour table for one network device.
 * @details Folds RTM_NEWNEIGH / RTM_DELNEIGH into a map of address to \c ndm_state. Holds facts only;
 * whether "no neighbour alive" means the peer is gone is the caller's policy. Matches by address, the
 * interface index is not checked: feed one table per device, \ref link_tracker::ifindex is the usual
 * filter. Pure: no socket, no syscalls. Not synchronized; feed it from one thread.
 */
class neighbor_table {
public:
    /**
     * @brief Upper bound on stored entries.
     * @details Known entries keep being refreshed; \ref update::alive still reports a live peer that did not fit.
     */
    static constexpr std::size_t max_entries = 32;

    /**
     * @brief What one announcement meant.
     */
    struct update {
        /**
         * @brief The announcement carried an NDA_DST address.
         * @details Without one the entry can neither be stored nor retired; every other field is then unset.
         */
        bool identified{false};
        /// The entry is now in the table. False for a removal, and for a new address at \ref max_entries.
        bool tracked{false};
        /**
         * @brief This announcement by itself describes a live peer.
         * @details Independent of whether the entry was stored within \ref max_entries.
         */
        bool alive{false};
        /**
         * @brief This announcement carries NUD_FAILED.
         * @details Read straight off \c ndm_state, also for a removal, unlike \ref alive: the kernel dropping
         * a failed entry is still a failure, while dropping a reachable one is not a live peer.
         */
        bool failed{false};
        /**
         * @brief The neighbour just became NUD_REACHABLE and carried a 48 bit link layer address.
         * @details Empty otherwise. Only NUD_REACHABLE is a fresh confirmation from the peer; NUD_STALE
         * and friends count as alive but are not new evidence.
         */
        std::string reachable_mac{};
    };

    /**
     * @brief Fold one neighbour announcement into the table.
     * @param[in] report The announcement, as decoded by \ref parse
     * @return What the announcement meant
     */
    update apply(neighbor_report const& report);

    /**
     * @brief Whether any stored entry is in a state that counts as alive.
     * @details States as in \ref is_neighbor_alive, except a NUD_STALE entry whose MAC is suspect. Entries
     * sharing a MAC are one station (on MCS its IPv4 and IPv6 link-local addresses); once the probed one
     * failed, an idle twin the kernel never re-probes would otherwise veto the loss verdict indefinitely.
     * Suspicion is a latch: set on NUD_FAILED (attributed via the remembered MAC; the FAILED announcement
     * usually carries no NDA_LLADDR), cleared only by a fresh NUD_REACHABLE of that MAC. A momentary test
     * fails because the kernel cycles FAILED -> INCOMPLETE -> FAILED and garbage collects FAILED in seconds.
     * @return True if at least one neighbour is alive, false otherwise
     */
    bool any_alive() const;

    /**
     * @brief Forget every entry.
     * @details Use when the table stops describing the link (carrier drop, device gone, peer
     * disconnected); the kernel does not reliably announce removals then.
     */
    void clear();

    /**
     * @brief Whether the table holds no entries.
     * @details Also the state after the kernel garbage-collected an idle entry.
     * @return True if no entry is stored, false otherwise
     */
    bool empty() const;

    /**
     * @brief The number of stored entries.
     * @return The entry count, at most \ref max_entries
     */
    std::size_t size() const;

private:
    struct entry {
        std::uint16_t nud_state{0};
        /**
         * @brief Last known link layer address of this neighbour.
         * @details Kept across announcements without NDA_LLADDR (NUD_FAILED usually has none) to attribute failures.
         */
        std::string mac;
    };
    /// Printable NDA_DST (see \ref neighbor_report::address) to what is known about it.
    std::map<std::string, entry> m_entries;
    /**
     * @brief Link layer addresses whose entries failed since their last fresh confirmation.
     * @details See any_alive(). Set on NUD_FAILED, cleared on NUD_REACHABLE of the same MAC, kept when the
     * failed entry is deleted. Bounded by \ref max_entries; at the cap new suspicions are dropped (errs alive).
     */
    std::set<std::string> m_suspect_macs;
};

} // namespace everest::lib::io::netlink
