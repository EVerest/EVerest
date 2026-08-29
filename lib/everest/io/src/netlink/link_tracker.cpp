// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/netlink/link_tracker.hpp>

#include <utility>

namespace everest::lib::io::netlink {

link_tracker::link_tracker(std::string device) : m_device(std::move(device)) {
}

bool link_tracker::is_tracked(link_report const& report) const {
    if (not report.name.empty()) {
        return report.name == m_device;
    }
    // No IFLA_IFNAME to go by: only an index we have already identified can still match.
    return m_ifindex != 0 and report.ifindex == m_ifindex;
}

link_tracker::change link_tracker::apply(link_report const& report) {
    change result;

    if (not is_tracked(report)) {
        // Our index turning up under a different name means the device was renamed away from
        // under us. There is no device by the configured name any more, so this is a removal.
        if (m_ifindex != 0 and report.ifindex == m_ifindex and not report.name.empty()) {
            m_ifindex = 0;
            set_carrier(false, result);
            set_presence(false, result);
        }
        return result;
    }

    if (report.deleted) {
        m_ifindex = 0;
        set_carrier(false, result);
        set_presence(false, result);
        return result;
    }

    m_ifindex = report.ifindex;
    set_presence(true, result);
    set_carrier(is_carrier_up(report), result);
    return result;
}

void link_tracker::set_presence(bool present, change& result) {
    if (m_present == present) {
        return;
    }
    m_present = present;
    result.presence_changed = true;
    result.present = present;
}

void link_tracker::set_carrier(bool carrier, change& result) {
    if (m_carrier == carrier) {
        return;
    }
    m_carrier = carrier;
    result.carrier_changed = true;
    result.carrier = carrier;
}

bool link_tracker::present() const {
    return m_present;
}

bool link_tracker::carrier() const {
    return m_carrier;
}

int link_tracker::ifindex() const {
    return m_ifindex;
}

std::string const& link_tracker::device() const {
    return m_device;
}

} // namespace everest::lib::io::netlink
