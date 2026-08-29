// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
//
// Tests for netlink/link_tracker.cpp: which announcement is my device, and when did its carrier
// change. Pure, so no socket and no privileges are involved.
//
// Every report below deliberately carries IFF_RUNNING, because the contract this class documents
// is that carrier is IFF_LOWER_UP and IFF_RUNNING must never be mistaken for it: a TAP device
// created carrier-off is announced once with IFF_RUNNING set, since the operstate that flag
// reflects is only corrected by the kernel's linkwatch work about a second later.

#include <gtest/gtest.h>

#include <string>

#include <everest/io/netlink/link_tracker.hpp>

namespace {

using namespace everest::lib::io::netlink;

constexpr char tracked_device[] = "cb_plc";

link_report make_link(std::string name, int ifindex, bool lower_up, bool deleted = false) {
    link_report report;
    report.name = std::move(name);
    report.ifindex = ifindex;
    report.lower_up = lower_up;
    report.admin_up = true;
    report.running = true;
    report.deleted = deleted;
    return report;
}

TEST(NetlinkLinkTracker, StartsWithNothingKnown) {
    link_tracker tracker(tracked_device);

    EXPECT_FALSE(tracker.present());
    EXPECT_FALSE(tracker.carrier());
    EXPECT_EQ(0, tracker.ifindex());
    EXPECT_EQ(std::string(tracked_device), tracker.device());
}

TEST(NetlinkLinkTracker, OtherDevicesAreIgnored) {
    link_tracker tracker(tracked_device);

    auto const change = tracker.apply(make_link("eth0", 2, true));

    EXPECT_FALSE(change.presence_changed);
    EXPECT_FALSE(change.carrier_changed);
    EXPECT_FALSE(tracker.present());
    EXPECT_EQ(0, tracker.ifindex());
}

TEST(NetlinkLinkTracker, AppearingCarrierOffReportsPresenceOnly) {
    link_tracker tracker(tracked_device);

    auto const change = tracker.apply(make_link(tracked_device, 7, false));

    EXPECT_TRUE(change.presence_changed);
    EXPECT_TRUE(change.present);
    EXPECT_FALSE(change.carrier_changed) << "IFF_RUNNING is set in the report and must not count";
    EXPECT_TRUE(tracker.present());
    EXPECT_FALSE(tracker.carrier());
    EXPECT_EQ(7, tracker.ifindex());
}

TEST(NetlinkLinkTracker, CarrierEdgesAreReportedOnce) {
    link_tracker tracker(tracked_device);
    (void)tracker.apply(make_link(tracked_device, 7, false));

    auto up = tracker.apply(make_link(tracked_device, 7, true));
    EXPECT_TRUE(up.carrier_changed);
    EXPECT_TRUE(up.carrier);
    EXPECT_FALSE(up.presence_changed);
    EXPECT_TRUE(tracker.carrier());

    auto repeat = tracker.apply(make_link(tracked_device, 7, true));
    EXPECT_FALSE(repeat.carrier_changed) << "a repeated announcement is not an edge";

    auto down = tracker.apply(make_link(tracked_device, 7, false));
    EXPECT_TRUE(down.carrier_changed);
    EXPECT_FALSE(down.carrier);
    EXPECT_FALSE(tracker.carrier());
}

TEST(NetlinkLinkTracker, ADeviceThatAppearsWithCarrierReportsBothEdges) {
    link_tracker tracker(tracked_device);

    auto const change = tracker.apply(make_link(tracked_device, 7, true));

    EXPECT_TRUE(change.presence_changed);
    EXPECT_TRUE(change.present);
    EXPECT_TRUE(change.carrier_changed);
    EXPECT_TRUE(change.carrier);
}

TEST(NetlinkLinkTracker, DeletionDropsCarrierPresenceAndTheIndex) {
    link_tracker tracker(tracked_device);
    (void)tracker.apply(make_link(tracked_device, 7, true));

    auto const change = tracker.apply(make_link(tracked_device, 7, true, true));

    EXPECT_TRUE(change.carrier_changed);
    EXPECT_FALSE(change.carrier);
    EXPECT_TRUE(change.presence_changed);
    EXPECT_FALSE(change.present);
    EXPECT_EQ(0, tracker.ifindex()) << "a re-created device gets a new index; keeping the old one "
                                       "would misattribute another device's neighbours";
}

TEST(NetlinkLinkTracker, ARecreatedDeviceIsIdentifiedByItsNewIndex) {
    link_tracker tracker(tracked_device);
    (void)tracker.apply(make_link(tracked_device, 7, true));
    (void)tracker.apply(make_link(tracked_device, 7, true, true));

    auto const change = tracker.apply(make_link(tracked_device, 11, false));

    EXPECT_TRUE(change.presence_changed);
    EXPECT_EQ(11, tracker.ifindex());
}

TEST(NetlinkLinkTracker, NamelessReportsAreMatchedByTheKnownIndex) {
    link_tracker tracker(tracked_device);
    (void)tracker.apply(make_link(tracked_device, 7, false));

    auto const change = tracker.apply(make_link("", 7, true));

    EXPECT_TRUE(change.carrier_changed);
    EXPECT_TRUE(change.carrier);
}

TEST(NetlinkLinkTracker, NamelessReportsForAnUnknownIndexAreIgnored) {
    link_tracker tracker(tracked_device);

    auto const change = tracker.apply(make_link("", 7, true));

    EXPECT_FALSE(change.presence_changed);
    EXPECT_FALSE(change.carrier_changed);
    EXPECT_FALSE(tracker.present());
}

TEST(NetlinkLinkTracker, RenamingTheDeviceAwayCountsAsRemoval) {
    link_tracker tracker(tracked_device);
    (void)tracker.apply(make_link(tracked_device, 7, true));

    auto const change = tracker.apply(make_link("cb_plc_old", 7, true));

    EXPECT_TRUE(change.carrier_changed);
    EXPECT_FALSE(change.carrier);
    EXPECT_TRUE(change.presence_changed);
    EXPECT_FALSE(change.present);
    EXPECT_EQ(0, tracker.ifindex());
}

} // namespace
