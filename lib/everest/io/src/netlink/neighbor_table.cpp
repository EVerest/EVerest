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

    // Judged from the announcement, not the table: a live peer beyond max_entries still counts.
    result.alive = not report.deleted and is_neighbor_alive(report.nud_state);
    result.failed = is_neighbor_failed(report.nud_state);
    if (not report.deleted and (report.nud_state & NUD_REACHABLE) != 0 and not report.mac.empty()) {
        result.reachable_mac = report.mac;
    }

    // Station MAC: from the announcement, else from the table. A NUD_FAILED announcement typically carries
    // no NDA_LLADDR, and a deletion carries the entry's final state; both need attribution (see any_alive()).
    auto const known = m_entries.find(report.address);
    std::string station_mac = report.mac;
    if (station_mac.empty() and known != m_entries.end()) {
        station_mac = known->second.mac;
    }

    // NUD_FAILED marks the station suspect, NUD_REACHABLE (the only new evidence from the peer) clears it.
    // With both bits set (possible on the wire, not from the kernel) recovery wins.
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
        // The suspicion stays: garbage collection of a failed entry is not evidence of life.
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
    // At the cap a new address is not stored; the result still describes the announcement in full.

    return result;
}

bool neighbor_table::any_alive() const {
    for (auto const& [address, e] : m_entries) {
        if (not is_neighbor_alive(e.nud_state)) {
            continue;
        }
        // A STALE entry of a suspect station is not evidence of life (see the header). DELAY and PROBE
        // resolve to REACHABLE or FAILED on their own and keep counting.
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
    // Suspicions belong to the link being forgotten.
    m_suspect_macs.clear();
}

bool neighbor_table::empty() const {
    return m_entries.empty();
}

std::size_t neighbor_table::size() const {
    return m_entries.size();
}

} // namespace everest::lib::io::netlink
