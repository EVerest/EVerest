// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/netlink/peer_liveness.hpp>

namespace everest::lib::io::netlink {

peer_liveness::verdict peer_liveness::apply(neighbor_report const& report) {
    verdict result;

    auto const update = m_table.apply(report);
    if (not update.identified) {
        // The report had no address, so it says nothing about the device's neighbours.
        return result;
    }

    // `update.alive` rather than only the table: at the table's cap a live peer's entry may not
    // have been stored, and it must still count.
    if (m_table.any_alive() or update.alive) {
        result.cancel_grace = true;
        result.reachable_mac = update.reachable_mac;
        return result;
    }

    if (m_table.empty()) {
        // No opinion: either nothing has been seen yet, or the last entry was removed rather than
        // failing. Removal is what the kernel does to idle entries, so it must not end a session.
        result.cancel_grace = true;
        return result;
    }

    if (update.failed) {
        result.arm_grace = true;
        return result;
    }

    // Neither alive nor failed (NUD_INCOMPLETE while resolution is in flight, NUD_NONE): leave a
    // running grace timer alone and do not start one - probing is not yet a verdict.
    return result;
}

bool peer_liveness::peer_is_lost() const {
    return not m_table.empty() and not m_table.any_alive();
}

void peer_liveness::clear() {
    m_table.clear();
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
