// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <vector>

#include "pp_ampacity_forwarder.hpp"

namespace {

using module::PpAmpacityForwarder;
using types::board_support_common::Ampacity;
using types::board_support_common::ProximityPilot;

ProximityPilot pp(Ampacity a) {
    ProximityPilot p;
    p.ampacity = a;
    return p;
}

TEST(PpAmpacityForwarderTest, value_published_before_connect_is_replayed) {
    // A cable already in the socket at boot: the BSP publishes before EvseManager's ready().
    PpAmpacityForwarder forwarder;
    forwarder.publish(pp(Ampacity::A_32));

    std::vector<Ampacity> received;
    forwarder.connect([&received](const ProximityPilot& p) { received.push_back(p.ampacity); });

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received[0], Ampacity::A_32);
}

TEST(PpAmpacityForwarderTest, only_latest_value_before_connect_is_replayed) {
    PpAmpacityForwarder forwarder;
    forwarder.publish(pp(Ampacity::A_32));
    forwarder.publish(pp(Ampacity::None));

    std::vector<Ampacity> received;
    forwarder.connect([&received](const ProximityPilot& p) { received.push_back(p.ampacity); });

    ASSERT_EQ(received.size(), 1);
    EXPECT_EQ(received[0], Ampacity::None);
}

TEST(PpAmpacityForwarderTest, nothing_replayed_without_prior_publish) {
    PpAmpacityForwarder forwarder;

    std::vector<Ampacity> received;
    forwarder.connect([&received](const ProximityPilot& p) { received.push_back(p.ampacity); });

    EXPECT_TRUE(received.empty());
}

TEST(PpAmpacityForwarderTest, values_after_connect_are_forwarded_in_order) {
    PpAmpacityForwarder forwarder;
    forwarder.publish(pp(Ampacity::A_13));

    std::vector<Ampacity> received;
    forwarder.connect([&received](const ProximityPilot& p) { received.push_back(p.ampacity); });
    forwarder.publish(pp(Ampacity::A_20));
    forwarder.publish(pp(Ampacity::None));

    ASSERT_EQ(received.size(), 3);
    EXPECT_EQ(received[0], Ampacity::A_13);
    EXPECT_EQ(received[1], Ampacity::A_20);
    EXPECT_EQ(received[2], Ampacity::None);
}

} // namespace
