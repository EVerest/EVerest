// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/netlink/peer_liveness.hpp>

namespace everest::lib::io::netlink {

peer_liveness::verdict peer_liveness::apply(neighbor_report const& report) {
    verdict result;

    auto const update = m_table.apply(report);
    if (not update.identified) {
        // No address: the report says nothing about the neighbours.
        return result;
    }

    // update.alive as well: at the table's cap a live peer's entry may not have been stored.
    if (m_table.any_alive() or update.alive) {
        m_seen_alive = true;
        m_last_entry_failed = false;
        result.cancel_grace = true;
        result.reachable_mac = update.reachable_mac;
        return result;
    }

    if (not m_seen_alive) {
        // A peer that never answered has not been lost: it may still be starting up. No grace can be running.
        return result;
    }

    if (m_table.empty()) {
        if (report.deleted and update.failed) {
            // The kernel garbage collected a NUD_FAILED entry, which happens within seconds of the failure,
            // before a useful grace period expires. Keep or start the grace timer and remember the loss.
            m_last_entry_failed = true;
            result.arm_grace = true;
            return result;
        }
        // Nothing seen yet, or the last entry was removed while not failed (idle removal); not a loss.
        m_last_entry_failed = false;
        result.cancel_grace = true;
        return result;
    }

    if (update.failed) {
        result.arm_grace = true;
        return result;
    }

    // Neither alive nor failed (NUD_INCOMPLETE, NUD_NONE): leave the grace timer as it is.
    return result;
}

bool peer_liveness::peer_is_lost() const {
    if (not m_seen_alive) {
        return false;
    }
    if (m_last_entry_failed) {
        return true;
    }
    return not m_table.empty() and not m_table.any_alive();
}

void peer_liveness::clear() {
    m_table.clear();
    m_last_entry_failed = false;
    m_seen_alive = false;
}

bool peer_liveness::empty() const {
    return m_table.empty();
}

std::size_t peer_liveness::size() const {
    return m_table.size();
}

bool peer_liveness::any_alive() const {
    return m_table.any_alive();
}

} // namespace everest::lib::io::netlink
