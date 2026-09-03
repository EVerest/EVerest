// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
//
// Tests for netlink/peer_liveness.cpp: what to conclude from the kernel neighbour table of a
// point-to-point device. The table mirroring itself is tested in netlink_neighbor_table_test.cpp;
// these cases pin down the judgement, which is the part that decides whether a working session gets
// torn down.

#include <gtest/gtest.h>

#include <string>

#include <linux/neighbour.h>

#include <everest/io/netlink/peer_liveness.hpp>

namespace {

using namespace everest::lib::io::netlink;

everest::lib::io::netlink::neighbor_report make_neighbor(std::string address, std::uint16_t state, std::string mac = {},
                                                         bool deleted = false) {
    everest::lib::io::netlink::neighbor_report report;
    report.ifindex = 7;
    report.address = std::move(address);
    report.nud_state = state;
    report.mac = std::move(mac);
    report.deleted = deleted;
    return report;
}

TEST(NetlinkPeerLiveness, AnEmptyTableHasNoOpinion) {
    peer_liveness tracker;

    EXPECT_TRUE(tracker.empty());
    EXPECT_FALSE(tracker.any_alive());
    EXPECT_FALSE(tracker.peer_is_lost()) << "nothing seen yet is not a lost link";
}

TEST(NetlinkPeerLiveness, EntriesWithoutAnAddressAreNotTracked) {
    peer_liveness tracker;

    auto const verdict = tracker.apply(make_neighbor("", NUD_FAILED));

    EXPECT_FALSE(verdict.arm_grace);
    EXPECT_FALSE(verdict.cancel_grace);
    EXPECT_TRUE(tracker.empty());
}

TEST(NetlinkPeerLiveness, AReachableNeighbourCancelsTheGraceAndOffersItsMac) {
    peer_liveness tracker;

    auto const verdict = tracker.apply(make_neighbor("fe80::1", NUD_REACHABLE, "0A:1B:2C:D3:E4:F5"));

    EXPECT_TRUE(verdict.cancel_grace);
    EXPECT_FALSE(verdict.arm_grace);
    EXPECT_EQ("0A:1B:2C:D3:E4:F5", verdict.reachable_mac);
    EXPECT_TRUE(tracker.any_alive());
    EXPECT_FALSE(tracker.peer_is_lost());
}

TEST(NetlinkPeerLiveness, StaleCountsAsAliveAndOffersNoMac) {
    peer_liveness tracker;

    auto const verdict = tracker.apply(make_neighbor("fe80::1", NUD_STALE, "0A:1B:2C:D3:E4:F5"));

    EXPECT_TRUE(verdict.cancel_grace);
    EXPECT_TRUE(verdict.reachable_mac.empty()) << "only NUD_REACHABLE is a fresh confirmation";
    EXPECT_TRUE(tracker.any_alive());
}

TEST(NetlinkPeerLiveness, TheLastNeighbourFailingArmsTheGraceAndIsALostLink) {
    peer_liveness tracker;
    (void)tracker.apply(make_neighbor("fe80::1", NUD_REACHABLE, "0A:1B:2C:D3:E4:F5"));

    auto const verdict = tracker.apply(make_neighbor("fe80::1", NUD_FAILED));

    EXPECT_TRUE(verdict.arm_grace);
    EXPECT_FALSE(verdict.cancel_grace);
    EXPECT_TRUE(tracker.peer_is_lost());
}

// The debounce this policy exists for: one address failing and re-resolving must not end a session.
TEST(NetlinkPeerLiveness, AFailedNeighbourThatRecoversCancelsTheGrace) {
    peer_liveness tracker;
    (void)tracker.apply(make_neighbor("fe80::1", NUD_REACHABLE, "0A:1B:2C:D3:E4:F5"));
    ASSERT_TRUE(tracker.apply(make_neighbor("fe80::1", NUD_FAILED)).arm_grace);

    auto const verdict = tracker.apply(make_neighbor("fe80::1", NUD_REACHABLE, "0A:1B:2C:D3:E4:F5"));

    EXPECT_TRUE(verdict.cancel_grace);
    EXPECT_FALSE(tracker.peer_is_lost());
}

TEST(NetlinkPeerLiveness, OneFailedAddressDoesNotCountWhileAnotherIsAlive) {
    peer_liveness tracker;
    (void)tracker.apply(make_neighbor("fe80::1", NUD_REACHABLE, "0A:1B:2C:D3:E4:F5"));
    (void)tracker.apply(make_neighbor("2001:db8::1", NUD_REACHABLE, "0A:1B:2C:D3:E4:F5"));

    auto const verdict = tracker.apply(make_neighbor("2001:db8::1", NUD_FAILED));

    EXPECT_FALSE(verdict.arm_grace);
    EXPECT_TRUE(verdict.cancel_grace);
    EXPECT_FALSE(tracker.peer_is_lost());
    EXPECT_EQ(2u, tracker.size());
}

TEST(NetlinkPeerLiveness, AllAddressesFailingIsALostLink) {
    peer_liveness tracker;
    (void)tracker.apply(make_neighbor("fe80::1", NUD_REACHABLE, "0A:1B:2C:D3:E4:F5"));
    (void)tracker.apply(make_neighbor("2001:db8::1", NUD_REACHABLE, "0A:1B:2C:D3:E4:F5"));
    (void)tracker.apply(make_neighbor("2001:db8::1", NUD_FAILED));

    auto const verdict = tracker.apply(make_neighbor("fe80::1", NUD_FAILED));

    EXPECT_TRUE(verdict.arm_grace);
    EXPECT_TRUE(tracker.peer_is_lost());
}

// The kernel removes idle entries by itself. That is housekeeping, not the EV disappearing.
TEST(NetlinkPeerLiveness, RemovingTheLastEntryIsNoOpinionRatherThanALoss) {
    peer_liveness tracker;
    (void)tracker.apply(make_neighbor("fe80::1", NUD_REACHABLE, "0A:1B:2C:D3:E4:F5"));

    auto const verdict = tracker.apply(make_neighbor("fe80::1", NUD_REACHABLE, {}, true));

    EXPECT_TRUE(verdict.cancel_grace);
    EXPECT_FALSE(verdict.arm_grace);
    EXPECT_TRUE(tracker.empty());
    EXPECT_FALSE(tracker.peer_is_lost());
}

// The kernel also garbage collects FAILED entries, within seconds - usually before a grace period of
// any useful length has expired. That removal is the dead peer being tidied away, and it must not
// cancel the verdict the failure produced (bench-found: the loss was silently never reported).
TEST(NetlinkPeerLiveness, RemovingTheLastFailedEntryKeepsTheLossVerdict) {
    peer_liveness tracker;
    (void)tracker.apply(make_neighbor("fe80::1", NUD_REACHABLE, "0A:1B:2C:D3:E4:F5"));
    ASSERT_TRUE(tracker.apply(make_neighbor("fe80::1", NUD_FAILED)).arm_grace);

    auto const verdict = tracker.apply(make_neighbor("fe80::1", NUD_FAILED, {}, true));

    EXPECT_FALSE(verdict.cancel_grace) << "garbage collection of a dead entry is not evidence of life";
    EXPECT_TRUE(verdict.arm_grace) << "and keeps (or starts) the grace period";
    EXPECT_TRUE(tracker.empty());
    EXPECT_TRUE(tracker.peer_is_lost()) << "the grace expiry must still find the peer lost";
}

// ... and the peer coming back afterwards is still a recovery.
TEST(NetlinkPeerLiveness, APeerRecoveringAfterItsFailedEntryWasRemovedCancelsTheGrace) {
    peer_liveness tracker;
    (void)tracker.apply(make_neighbor("fe80::1", NUD_FAILED));
    (void)tracker.apply(make_neighbor("fe80::1", NUD_FAILED, {}, true));
    ASSERT_TRUE(tracker.peer_is_lost());

    auto const verdict = tracker.apply(make_neighbor("fe80::1", NUD_REACHABLE, "0A:1B:2C:D3:E4:F5"));

    EXPECT_TRUE(verdict.cancel_grace);
    EXPECT_FALSE(tracker.peer_is_lost());
}

// A later removal of a healthy entry returns to "no opinion", the remembered loss included.
TEST(NetlinkPeerLiveness, RemovingAHealthyEntryAfterARememberedLossClearsIt) {
    peer_liveness tracker;
    (void)tracker.apply(make_neighbor("fe80::1", NUD_FAILED));
    (void)tracker.apply(make_neighbor("fe80::1", NUD_FAILED, {}, true));
    (void)tracker.apply(make_neighbor("fe80::1", NUD_REACHABLE, "0A:1B:2C:D3:E4:F5"));

    (void)tracker.apply(make_neighbor("fe80::1", NUD_REACHABLE, {}, true));

    EXPECT_TRUE(tracker.empty());
    EXPECT_FALSE(tracker.peer_is_lost());
}

TEST(NetlinkPeerLiveness, ResolutionInProgressNeitherArmsNorCancels) {
    peer_liveness tracker;

    auto const verdict = tracker.apply(make_neighbor("fe80::1", NUD_INCOMPLETE));

    EXPECT_FALSE(verdict.arm_grace) << "probing is not yet a verdict";
    EXPECT_FALSE(verdict.cancel_grace) << "and it must not retire a grace period already running";
    EXPECT_EQ(1u, tracker.size());
}

TEST(NetlinkPeerLiveness, ClearForgetsEverything) {
    peer_liveness tracker;
    (void)tracker.apply(make_neighbor("fe80::1", NUD_FAILED));
    ASSERT_FALSE(tracker.empty());

    tracker.clear();

    EXPECT_TRUE(tracker.empty());
    EXPECT_FALSE(tracker.peer_is_lost());
}

TEST(NetlinkPeerLiveness, TheTableDoesNotGrowWithoutBoundButKeepsUpdatingWhatItKnows) {
    peer_liveness tracker;
    for (int i = 0; i < 40; ++i) {
        (void)tracker.apply(make_neighbor("2001:db8::" + std::to_string(i), NUD_REACHABLE, "0A:1B:2C:D3:E4:F5"));
    }
    EXPECT_EQ(neighbor_table::max_entries, tracker.size());

    // An address that made it into the table is still tracked ...
    auto const known = tracker.apply(make_neighbor("2001:db8::0", NUD_FAILED));
    EXPECT_FALSE(known.arm_grace) << "the other 31 are still alive";

    // ... and one that did not is still not stored.
    auto const unknown = tracker.apply(make_neighbor("2001:db8::ffff", NUD_FAILED));
    EXPECT_FALSE(unknown.arm_grace);
    EXPECT_EQ(neighbor_table::max_entries, tracker.size());
}

// At the cap an untracked report is not stored, but it must still be allowed to prove the peer is
// alive - otherwise a table full of dead entries could tear down a session whose peer is answering.
TEST(NetlinkPeerLiveness, AnAliveReportCountsEvenWhenTheTableIsTooFullToStoreIt) {
    peer_liveness tracker;
    for (int i = 0; i < 32; ++i) {
        (void)tracker.apply(make_neighbor("2001:db8::" + std::to_string(i), NUD_REACHABLE, "0A:1B:2C:D3:E4:F5"));
    }
    for (int i = 0; i < 32; ++i) {
        (void)tracker.apply(make_neighbor("2001:db8::" + std::to_string(i), NUD_FAILED));
    }
    ASSERT_TRUE(tracker.peer_is_lost());

    auto const verdict = tracker.apply(make_neighbor("fe80::beef", NUD_REACHABLE, "0A:1B:2C:D3:E4:FF"));

    EXPECT_TRUE(verdict.cancel_grace) << "the peer is demonstrably up";
    EXPECT_FALSE(verdict.arm_grace);
    EXPECT_EQ("0A:1B:2C:D3:E4:FF", verdict.reachable_mac);
    EXPECT_EQ(neighbor_table::max_entries, tracker.size()) << "still not stored";
}

} // namespace
