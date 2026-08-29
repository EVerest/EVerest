// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/netlink/neighbor_table.hpp>

#include <linux/neighbour.h>

namespace everest::lib::io::netlink {

neighbor_table::update neighbor_table::apply(neighbor_report const& report) {
    update result;

    if (report.address.empty()) {
        // Without NDA_DST the entry has no identity, so it can neither be stored nor retired.
        return result;
    }
    result.identified = true;

    // Judged from the announcement itself rather than from the stored state, so that a live peer
    // whose entry did not fit within max_entries is still reported as alive.
    result.alive = not report.deleted and is_neighbor_alive(report.nud_state);
    result.failed = is_neighbor_failed(report.nud_state);
    if (not report.deleted and (report.nud_state & NUD_REACHABLE) != 0 and not report.mac.empty()) {
        result.reachable_mac = report.mac;
    }

    // The MAC this announcement is ABOUT: what it carries, or what the table remembers for the
    // address. A NUD_FAILED announcement typically carries no NDA_LLADDR, and a deletion carries
    // the entry's final state - both still need attribution to their station (see any_alive()).
    auto const known = m_entries.find(report.address);
    std::string station_mac = report.mac;
    if (station_mac.empty() and known != m_entries.end()) {
        station_mac = known->second.mac;
    }

    // The suspicion latch: NUD_FAILED marks the station suspect, a fresh NUD_REACHABLE - the only
    // state that is new evidence from the peer - clears it. Order matters for an announcement
    // carrying both bits (the kernel does not, but the wire format could): recovery wins.
    if (not station_mac.empty()) {
        if (is_neighbor_failed(report.nud_state)) {
            if (m_suspect_macs.size() < max_entries) {
                m_suspect_macs.insert(station_mac);
            }
        }
        if (not report.deleted and (report.nud_state & NUD_REACHABLE) != 0) {
            m_suspect_macs.erase(station_mac);
        }
    }

    if (report.deleted) {
        // The entry goes; the suspicion, if any, deliberately stays - garbage collection of a
        // failed entry is bookkeeping, not evidence of life.
        m_entries.erase(report.address);
        return result;
    }

    if (known != m_entries.end()) {
        known->second.nud_state = report.nud_state;
        if (not report.mac.empty()) {
            known->second.mac = report.mac;
        }
        result.tracked = true;
    } else if (m_entries.size() < max_entries) {
        m_entries.emplace(report.address, entry{report.nud_state, report.mac});
        result.tracked = true;
    }
    // At the cap a new address is not stored. The announcement is still described in full, so a
    // caller can act on it without the table growing without bound.

    return result;
}

bool neighbor_table::any_alive() const {
    for (auto const& [address, e] : m_entries) {
        if (not is_neighbor_alive(e.nud_state)) {
            continue;
        }
        // The same-station refinement (see the header): a STALE entry of a suspect station is a
        // ghost of that dead station, not evidence of life. Actively-verifying states (DELAY,
        // PROBE) resolve to REACHABLE or FAILED on their own and keep counting meanwhile.
        if ((e.nud_state & NUD_STALE) != 0 and not e.mac.empty() and
            m_suspect_macs.find(e.mac) != m_suspect_macs.end()) {
            continue;
        }
        return true;
    }
    return false;
}

void neighbor_table::clear() {
    m_entries.clear();
    // Suspicions describe stations of the link being forgotten - a fresh link starts unprejudiced.
    m_suspect_macs.clear();
}

bool neighbor_table::empty() const {
    return m_entries.empty();
}

std::size_t neighbor_table::size() const {
    return m_entries.size();
}

} // namespace everest::lib::io::netlink
