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
 * @brief A mirror of the kernel's neighbour table for one network device.
 *
 * @details Folds RTM_NEWNEIGH / RTM_DELNEIGH announcements into a map of address to \c ndm_state
 * and answers the questions a liveness or reachability policy is built on: is any neighbour of this
 * device alive, how many are known, and what did this particular announcement mean.
 *
 * The table deliberately holds facts only and draws no conclusions: whether "no neighbour is alive"
 * means the peer is gone, and how long to wait before saying so, is a policy decision that depends
 * on the link technology and belongs to the caller.
 *
 * Neighbour discovery is worth mirroring because it is the one peer signal that costs nothing and
 * works on any netdev: the kernel actively probes, so the NUD state machine notices a peer that
 * stopped answering even when the local PHY still reports carrier.
 *
 * Announcements are matched by address, so feed one table per device - the interface index of a
 * report is not checked here. \ref link_tracker::ifindex is the usual filter.
 *
 * Pure: no socket and no syscalls. Not synchronized; feed it from one thread.
 */
class neighbor_table {
public:
    /**
     * @brief Upper bound on stored entries.
     * @details A point to point link has one or two neighbours (a link-local address and possibly a
     * global one). The cap exists only so that a device with many peers cannot grow this map
     * without bound; entries already known keep being refreshed, and \ref update::alive still
     * reports a live peer whose entry did not fit.
     */
    static constexpr std::size_t max_entries = 32;

    /**
     * @brief What one announcement meant.
     */
    struct update {
        /**
         * @brief The announcement carried an NDA_DST address.
         * @details Without one an entry has no identity: it can neither be stored nor retired, and
         * the announcement says nothing about the device's neighbours. Every other field is then
         * false or empty.
         */
        bool identified{false};
        /// The entry is now in the table. False for a removal, and for a new address at \ref
        /// max_entries.
        bool tracked{false};
        /**
         * @brief This announcement by itself describes a live peer.
         * @details Independent of whether the entry was stored, so a caller can still recognise a
         * live peer whose entry did not fit within \ref max_entries.
         */
        bool alive{false};
        /**
         * @brief This announcement carries NUD_FAILED.
         * @details Read straight off \c ndm_state, deliberately unlike \ref alive, which is
         * additionally false for a removal. A removal carries the state the entry had when it went
         * away, and "it was failed when the kernel dropped it" is information a caller may want,
         * whereas "it was reachable when the kernel dropped it" must not be mistaken for a live
         * peer. So: \ref alive answers "is the peer up right now", \ref failed answers "what did
         * this announcement say".
         */
        bool failed{false};
        /**
         * @brief The neighbour just became NUD_REACHABLE and carried a 48 bit link layer address.
         * @details Empty otherwise. NUD_REACHABLE is the only state that is a fresh confirmation
         * from the peer; NUD_STALE and friends count as alive but are not new evidence.
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
     * @details See \ref is_neighbor_alive for which states those are, with one refinement: a
     * NUD_STALE entry does not count while its link layer address is SUSPECT. Multiple entries
     * sharing a MAC are the same physical station (on an MCS link typically its IPv4 and IPv6
     * link-local addresses), and when the actively probed one has definitively failed, an idle
     * twin the kernel never re-probes is a ghost of that same dead station, not independent
     * evidence of life - left counted, it would veto the loss verdict indefinitely (bench-found).
     *
     * Suspicion is a LATCH, not a momentary state test: it is set when an entry reaches
     * NUD_FAILED (attributed through the remembered MAC - the FAILED announcement itself
     * typically carries no NDA_LLADDR) and cleared only by a fresh NUD_REACHABLE of that MAC.
     * Testing the twin's live state instead has two holes, both bench-found on the same day:
     * under an active sender the kernel cycles FAILED -> INCOMPLETE -> FAILED, and it garbage
     * collects failed entries within seconds - either way there is usually no entry reading
     * FAILED at the exact moment the caller's grace expires. Entries with distinct or unknown
     * MACs keep the documented stale-counts-as-alive semantics.
     * @return True if at least one neighbour is alive, false otherwise
     */
    bool any_alive() const;

    /**
     * @brief Forget every entry.
     * @details Appropriate whenever what the table holds stops describing the current link - the
     * carrier dropping, the device disappearing, the peer being disconnected. The kernel does not
     * reliably announce the removal of entries in those cases.
     */
    void clear();

    /**
     * @brief Whether the table holds no entries.
     * @details An empty table is the state before anything has been seen, and also the state after
     * the kernel garbage-collected an idle entry.
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
         * @brief The last known link layer address of this neighbour.
         * @details Kept across announcements that carry no NDA_LLADDR - a NUD_FAILED announcement
         * typically does not - so a failure can still be attributed to the station it belongs to.
         */
        std::string mac;
    };
    /// Printable NDA_DST (see \ref neighbor_report::address) to what is known about it.
    std::map<std::string, entry> m_entries;
    /**
     * @brief Link layer addresses whose entries have failed since their last fresh confirmation.
     * @details See any_alive(). Set on NUD_FAILED, cleared on NUD_REACHABLE of the same MAC, and
     * deliberately NOT cleared when the failed entry is deleted - the kernel garbage collecting a
     * failed entry is bookkeeping, not evidence of life. Bounded by \ref max_entries like the
     * table itself; at the cap new suspicions are dropped, which errs toward the pre-existing
     * (alive) semantics.
     */
    std::set<std::string> m_suspect_macs;
};

} // namespace everest::lib::io::netlink
