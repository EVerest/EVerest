// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <everest/io/netlink/route_parser.hpp>

#include <string>

namespace everest::lib::io::netlink {

/**
 * @brief Carrier and presence edge tracking for one named network device.
 *
 * @details Folds the stream of link announcements from a NETLINK_ROUTE socket into two questions -
 * "does my device exist" and "does it have carrier" - and reports only the changes, so a consumer
 * sees one event per actual transition rather than one per announcement.
 *
 * <b>The carrier contract.</b> Carrier is \c IFF_LOWER_UP in \c ifi_flags, and deliberately not
 * either of the alternatives. Picking either of them instead is a real bug, not a matter of taste:
 * - Not the \c operstate string. A TAP device does not maintain a meaningful operstate; it starts
 *   out \c UNKNOWN, which reads as running.
 * - Not \c IFF_RUNNING. A TAP device created carrier-off is still announced <b>once with
 *   IFF_RUNNING set</b>, because the operstate that flag reflects is only corrected by the kernel's
 *   linkwatch work about a second later. Keying on IFF_RUNNING therefore produces a spurious ~1 s
 *   carrier-up on every TAP creation and every device reset. This was measured, not assumed.
 *
 * Two consequences worth knowing for either flag choice:
 * - Because linkwatch is rate-limited to roughly one run per second, the announcement that
 *   <em>drops</em> IFF_LOWER_UP can lag the physical event by up to about a second.
 * - A carrier off-to-on edge makes the kernel re-run IPv6 duplicate address detection, so the
 *   device's link-local address stays unusable for roughly a second afterwards (with the default
 *   \c dad_transmits). Carrier-up is not the same as "IPv6 usable", and nothing may assume it can
 *   send the moment the carrier appears - a consumer either tolerates the first send failing or
 *   waits the address out.
 *
 * <b>Identification is by name</b>, because the interface index is not known in advance and a
 * re-created device (a TAP, typically) gets a fresh one every time. The index learned from a named
 * announcement is what nameless announcements are matched against afterwards, and it is dropped the
 * moment the device goes away, so a recycled index cannot make another device's messages - or its
 * neighbour entries - look like this device's.
 *
 * Pure: no socket and no syscalls, so the identification and edge-filtering rules are testable on
 * their own. Not synchronized; feed it from one thread.
 */
class link_tracker {
public:
    /**
     * @brief What changed as a result of folding in one announcement.
     * @details Both edges can be set at once: a device that appears already having carrier reports
     * presence and carrier together.
     */
    struct change {
        /// \ref present differs from what it was before the announcement.
        bool presence_changed{false};
        /// Whether the device exists, valid when \ref presence_changed is set.
        bool present{false};
        /// \ref carrier differs from what it was before the announcement.
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
     * @details Announcements for other devices are ignored. An announcement carrying the tracked
     * interface index under a different name means the device was renamed away, which is reported
     * as a removal.
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
     * @brief The carrier state of the tracked device (\c IFF_LOWER_UP).
     * @details Always false while the device is absent.
     * @return True if the device has carrier, false otherwise
     */
    bool carrier() const;

    /**
     * @brief The interface index of the tracked device.
     * @details Useful for matching messages that carry no device name, neighbour announcements in
     * particular.
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
