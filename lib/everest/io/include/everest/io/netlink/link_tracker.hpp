// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <everest/io/netlink/route_parser.hpp>

#include <string>

namespace everest::lib::io::netlink {

/**
 * @brief Carrier and presence edge tracking for one named network device.
 * @details Folds link announcements from a NETLINK_ROUTE socket into presence and carrier, reporting only
 * transitions. Carrier is \c IFF_LOWER_UP in \c ifi_flags, not the \c operstate string (a TAP starts out
 * \c UNKNOWN, which reads as running) and not \c IFF_RUNNING (a TAP created carrier-off is announced once
 * with IFF_RUNNING set until linkwatch corrects the operstate about a second later: a spurious ~1 s
 * carrier-up on every TAP creation and device reset, measured). Linkwatch runs about once per second, so
 * the announcement dropping IFF_LOWER_UP can lag the physical event by up to a second. A carrier off-to-on
 * edge re-runs IPv6 duplicate address detection, so the link-local address is unusable for about a second
 * (default \c dad_transmits); carrier-up is not "IPv6 usable". Identification is by name, since the index
 * is not known in advance and a re-created device gets a fresh one; the learned index matches nameless
 * announcements and is dropped when the device goes away, so a recycled index cannot attribute another
 * device's messages or neighbour entries to this one. Pure: no socket, no syscalls. Not synchronized;
 * feed it from one thread.
 */
class link_tracker {
public:
    /**
     * @brief What changed as a result of one announcement.
     * @details Both edges can be set at once: a device appearing with carrier reports both.
     */
    struct change {
        /// \ref present changed.
        bool presence_changed{false};
        /// Whether the device exists, valid when \ref presence_changed is set.
        bool present{false};
        /// \ref carrier changed.
        bool carrier_changed{false};
        /// Whether the device has carrier, valid when \ref carrier_changed is set.
        bool carrier{false};
    };

    /**
     * @brief Constructor
     * @param[in] device The name of the network device to track
     */
    explicit link_tracker(std::string device);

    /**
     * @brief Fold one link announcement into the tracked state.
     * @details Other devices are ignored. The tracked index under another name is a rename away, reported as removal.
     * @param[in] report The announcement, as decoded by \ref parse
     * @return The edges this announcement caused, if any
     */
    change apply(link_report const& report);

    /**
     * @brief Whether the tracked device currently exists.
     * @return True if the device is present, false otherwise
     */
    bool present() const;

    /**
     * @brief The carrier state of the tracked device (\c IFF_LOWER_UP). Always false while absent.
     * @return True if the device has carrier, false otherwise
     */
    bool carrier() const;

    /**
     * @brief The interface index of the tracked device; matches nameless messages such as neighbour ones.
     * @return The interface index, or 0 while it is unknown
     */
    int ifindex() const;

    /**
     * @brief The device name this tracker was constructed with.
     * @return The device name
     */
    std::string const& device() const;

private:
    bool is_tracked(link_report const& report) const;
    void set_presence(bool present, change& result);
    void set_carrier(bool carrier, change& result);

    std::string m_device;
    int m_ifindex{0};
    bool m_present{false};
    bool m_carrier{false};
};

} // namespace everest::lib::io::netlink
