// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
//
// Tests for netlink/neighbor_table.cpp, the mirror of the kernel neighbour table for one device; facts only.

#include <gtest/gtest.h>

#include <string>

#include <linux/neighbour.h>

#include <everest/io/netlink/neighbor_table.hpp>

namespace {

using namespace everest::lib::io::netlink;

neighbor_report make_neighbor(std::string address, std::uint16_t state, std::string mac = {}, bool deleted = false) {
    neighbor_report report;
    report.ifindex = 7;
    report.address = std::move(address);
    report.nud_state = state;
    report.mac = std::move(mac);
    report.deleted = deleted;
    return report;
}

TEST(NetlinkNeighborTable, StartsEmpty) {
    neighbor_table table;

    EXPECT_TRUE(table.empty());
    EXPECT_EQ(0u, table.size());
    EXPECT_FALSE(table.any_alive());
}

// Without NDA_DST the entry has no identity and can neither be stored nor retired.
TEST(NetlinkNeighborTable, AnAnnouncementWithoutAnAddressIsNotIdentified) {
    neighbor_table table;

    auto const update = table.apply(make_neighbor("", NUD_REACHABLE, "0A:1B:2C:D3:E4:F5"));

    EXPECT_FALSE(update.identified);
    EXPECT_FALSE(update.tracked);
    EXPECT_FALSE(update.alive);
    EXPECT_FALSE(update.failed);
    EXPECT_TRUE(update.reachable_mac.empty());
    EXPECT_TRUE(table.empty());
}

TEST(NetlinkNeighborTable, AReachableNeighbourIsStoredAliveAndOffersItsMac) {
    neighbor_table table;

    auto const update = table.apply(make_neighbor("fe80::1", NUD_REACHABLE, "0A:1B:2C:D3:E4:F5"));

    EXPECT_TRUE(update.identified);
    EXPECT_TRUE(update.tracked);
    EXPECT_TRUE(update.alive);
    EXPECT_FALSE(update.failed);
    EXPECT_EQ("0A:1B:2C:D3:E4:F5", update.reachable_mac);
    EXPECT_TRUE(table.any_alive());
    EXPECT_EQ(1u, table.size());
}

// NUD_STALE: was reachable, not re-verified. The kernel only re-probes on send, so an idle peer stays there.
TEST(NetlinkNeighborTable, StaleCountsAsAliveButIsNotFreshEvidence) {
    neighbor_table table;

    auto const update = table.apply(make_neighbor("fe80::1", NUD_STALE, "0A:1B:2C:D3:E4:F5"));

    EXPECT_TRUE(update.alive);
    EXPECT_TRUE(update.reachable_mac.empty()) << "only NUD_REACHABLE is a fresh confirmation";
    EXPECT_TRUE(table.any_alive());
}

TEST(NetlinkNeighborTable, DelayProbeAndPermanentAlsoCountAsAlive) {
    for (std::uint16_t state : {NUD_DELAY, NUD_PROBE, NUD_PERMANENT}) {
        neighbor_table table;
        EXPECT_TRUE(table.apply(make_neighbor("fe80::1", state)).alive) << "state " << state;
        EXPECT_TRUE(table.any_alive()) << "state " << state;
    }
}

TEST(NetlinkNeighborTable, AFailedNeighbourIsStoredAndReportedAsFailed) {
    neighbor_table table;
    (void)table.apply(make_neighbor("fe80::1", NUD_REACHABLE, "0A:1B:2C:D3:E4:F5"));

    auto const update = table.apply(make_neighbor("fe80::1", NUD_FAILED));

    EXPECT_TRUE(update.tracked);
    EXPECT_FALSE(update.alive);
    EXPECT_TRUE(update.failed);
    EXPECT_FALSE(table.any_alive());
    EXPECT_EQ(1u, table.size()) << "refreshed in place, not duplicated";
}

TEST(NetlinkNeighborTable, RecoveryIsReportedAliveAgain) {
    neighbor_table table;
    (void)table.apply(make_neighbor("fe80::1", NUD_FAILED));
    ASSERT_FALSE(table.any_alive());

    auto const update = table.apply(make_neighbor("fe80::1", NUD_REACHABLE, "0A:1B:2C:D3:E4:F5"));

    EXPECT_TRUE(update.alive);
    EXPECT_TRUE(table.any_alive());
}

TEST(NetlinkNeighborTable, AnyAliveLooksAtEveryEntry) {
    neighbor_table table;
    (void)table.apply(make_neighbor("fe80::1", NUD_REACHABLE, "0A:1B:2C:D3:E4:F5"));
    (void)table.apply(make_neighbor("2001:db8::1", NUD_REACHABLE, "0A:1B:2C:D3:E4:F5"));

    (void)table.apply(make_neighbor("2001:db8::1", NUD_FAILED));
    EXPECT_TRUE(table.any_alive()) << "the other address is still reachable";
    EXPECT_EQ(2u, table.size());

    (void)table.apply(make_neighbor("fe80::1", NUD_FAILED));
    EXPECT_FALSE(table.any_alive());
}

TEST(NetlinkNeighborTable, ARemovalRetiresTheEntryAndIsNotTracked) {
    neighbor_table table;
    (void)table.apply(make_neighbor("fe80::1", NUD_REACHABLE, "0A:1B:2C:D3:E4:F5"));

    auto const update = table.apply(make_neighbor("fe80::1", NUD_REACHABLE, "0A:1B:2C:D3:E4:F5", true));

    EXPECT_TRUE(update.identified);
    EXPECT_FALSE(update.tracked);
    EXPECT_FALSE(update.alive) << "a removed entry is not a live peer";
    EXPECT_TRUE(update.reachable_mac.empty());
    EXPECT_TRUE(table.empty());
}

TEST(NetlinkNeighborTable, ResolutionInProgressIsNeitherAliveNorFailed) {
    neighbor_table table;

    auto const update = table.apply(make_neighbor("fe80::1", NUD_INCOMPLETE));

    EXPECT_TRUE(update.tracked);
    EXPECT_FALSE(update.alive);
    EXPECT_FALSE(update.failed);
    EXPECT_FALSE(table.any_alive());
    EXPECT_EQ(1u, table.size());
}

TEST(NetlinkNeighborTable, ClearForgetsEverything) {
    neighbor_table table;
    (void)table.apply(make_neighbor("fe80::1", NUD_REACHABLE, "0A:1B:2C:D3:E4:F5"));
    ASSERT_FALSE(table.empty());

    table.clear();

    EXPECT_TRUE(table.empty());
    EXPECT_FALSE(table.any_alive());
}

TEST(NetlinkNeighborTable, StorageStopsAtTheCapButKnownEntriesKeepUpdating) {
    neighbor_table table;
    for (int i = 0; i < 40; ++i) {
        (void)table.apply(make_neighbor("2001:db8::" + std::to_string(i), NUD_REACHABLE, "0A:1B:2C:D3:E4:F5"));
    }
    EXPECT_EQ(neighbor_table::max_entries, table.size());

    auto const known = table.apply(make_neighbor("2001:db8::0", NUD_FAILED));
    EXPECT_TRUE(known.tracked);
    EXPECT_TRUE(known.failed);

    auto const unknown = table.apply(make_neighbor("2001:db8::ffff", NUD_FAILED));
    EXPECT_FALSE(unknown.tracked) << "no room, so it is not stored";
    EXPECT_TRUE(unknown.identified) << "but it is still described";
    EXPECT_TRUE(unknown.failed);
    EXPECT_EQ(neighbor_table::max_entries, table.size());
}

// `alive` is judged from the announcement, not from storage.
TEST(NetlinkNeighborTable, AnAliveAnnouncementIsReportedEvenWhenItCannotBeStored) {
    neighbor_table table;
    for (std::size_t i = 0; i < neighbor_table::max_entries; ++i) {
        (void)table.apply(make_neighbor("2001:db8::" + std::to_string(i), NUD_FAILED));
    }
    ASSERT_EQ(neighbor_table::max_entries, table.size());
    ASSERT_FALSE(table.any_alive());

    auto const update = table.apply(make_neighbor("fe80::beef", NUD_REACHABLE, "0A:1B:2C:D3:E4:FF"));

    EXPECT_FALSE(update.tracked);
    EXPECT_TRUE(update.alive);
    EXPECT_EQ("0A:1B:2C:D3:E4:FF", update.reachable_mac);
    EXPECT_FALSE(table.any_alive()) << "the stored entries are still all dead";
}

// Bench case: one station, two addresses (IPv4 pinged into FAILED, IPv6 link-local idle in STALE, same MAC).
TEST(NetlinkNeighborTable, AStaleTwinOfAFailedStationDoesNotCountAsAlive) {
    neighbor_table table;
    (void)table.apply(make_neighbor("172.25.6.1", NUD_REACHABLE, "F2:0E:4F:18:21:BF"));
    (void)table.apply(make_neighbor("fe80::f00e:4fff:fe18:21bf", NUD_STALE, "F2:0E:4F:18:21:BF"));
    ASSERT_TRUE(table.any_alive());

    // The kernel's NUD_FAILED announcement carries no NDA_LLADDR; attribution uses the remembered MAC.
    (void)table.apply(make_neighbor("172.25.6.1", NUD_FAILED));

    EXPECT_FALSE(table.any_alive()) << "the STALE twin of the failed station vetoed the verdict";
}

TEST(NetlinkNeighborTable, AStaleNeighbourOfAnotherStationStillCountsAsAlive) {
    neighbor_table table;
    (void)table.apply(make_neighbor("172.25.6.1", NUD_FAILED, "F2:0E:4F:18:21:BF"));
    (void)table.apply(make_neighbor("172.25.6.3", NUD_STALE, "0A:1B:2C:D3:E4:F5"));

    EXPECT_TRUE(table.any_alive());
}

TEST(NetlinkNeighborTable, AStaleNeighbourWithoutAMacStillCountsAsAlive) {
    neighbor_table table;
    (void)table.apply(make_neighbor("172.25.6.1", NUD_FAILED, "F2:0E:4F:18:21:BF"));
    (void)table.apply(make_neighbor("fe80::dead", NUD_STALE));

    EXPECT_TRUE(table.any_alive());
}

// DELAY and PROBE are the kernel actively verifying.
TEST(NetlinkNeighborTable, AnActivelyVerifyingTwinStillCountsAsAlive) {
    neighbor_table table;
    (void)table.apply(make_neighbor("172.25.6.1", NUD_FAILED, "F2:0E:4F:18:21:BF"));
    (void)table.apply(make_neighbor("fe80::f00e:4fff:fe18:21bf", NUD_PROBE, "F2:0E:4F:18:21:BF"));

    EXPECT_TRUE(table.any_alive());
}

TEST(NetlinkNeighborTable, TheVetoLiftsWhenTheFailedTwinRecovers) {
    neighbor_table table;
    (void)table.apply(make_neighbor("fe80::f00e:4fff:fe18:21bf", NUD_STALE, "F2:0E:4F:18:21:BF"));
    (void)table.apply(make_neighbor("172.25.6.1", NUD_FAILED, "F2:0E:4F:18:21:BF"));
    ASSERT_FALSE(table.any_alive());

    (void)table.apply(make_neighbor("172.25.6.1", NUD_REACHABLE, "F2:0E:4F:18:21:BF"));

    EXPECT_TRUE(table.any_alive());
}

// Under an active sender the kernel cycles FAILED -> INCOMPLETE -> FAILED; at grace expiry no entry may read FAILED.
TEST(NetlinkNeighborTable, TheVetoSurvivesTheFailedIncompleteCycle) {
    neighbor_table table;
    (void)table.apply(make_neighbor("fe80::f00e:4fff:fe18:21bf", NUD_STALE, "F2:0E:4F:18:21:BF"));
    (void)table.apply(make_neighbor("172.25.6.1", NUD_REACHABLE, "F2:0E:4F:18:21:BF"));
    (void)table.apply(make_neighbor("172.25.6.1", NUD_FAILED));
    ASSERT_FALSE(table.any_alive());

    (void)table.apply(make_neighbor("172.25.6.1", NUD_INCOMPLETE)); // the next ping restarts resolution

    EXPECT_FALSE(table.any_alive()) << "INCOMPLETE is not evidence of life and must not lift the veto";
}

// The kernel garbage collects failed entries within seconds.
TEST(NetlinkNeighborTable, TheVetoSurvivesGarbageCollectionOfTheFailedEntry) {
    neighbor_table table;
    (void)table.apply(make_neighbor("fe80::f00e:4fff:fe18:21bf", NUD_STALE, "F2:0E:4F:18:21:BF"));
    (void)table.apply(make_neighbor("172.25.6.1", NUD_REACHABLE, "F2:0E:4F:18:21:BF"));
    (void)table.apply(make_neighbor("172.25.6.1", NUD_FAILED));
    ASSERT_FALSE(table.any_alive());

    (void)table.apply(make_neighbor("172.25.6.1", NUD_FAILED, "", true)); // gc'd

    EXPECT_FALSE(table.any_alive()) << "deleting the failed entry must not resurrect its stale twin";
}

TEST(NetlinkNeighborTable, ClearForgetsSuspicionsWithTheEntries) {
    neighbor_table table;
    (void)table.apply(make_neighbor("172.25.6.1", NUD_FAILED, "F2:0E:4F:18:21:BF"));
    table.clear();

    (void)table.apply(make_neighbor("fe80::f00e:4fff:fe18:21bf", NUD_STALE, "F2:0E:4F:18:21:BF"));

    EXPECT_TRUE(table.any_alive());
}

} // namespace
