// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

// The PLC keepalive frame re-teaches the ChargeBridge firmware the host's UDP endpoint. It goes onto
// the wire, so its layout must stay a harmless broadcast with the local-experimental EtherType.

#include <charge_bridge/plc_keepalive.hpp>
#include <gtest/gtest.h>

namespace {

using charge_bridge::mac_address;
using charge_bridge::make_plc_keepalive_frame;
using charge_bridge::parse_mac_address;

TEST(plc_keepalive, frame_is_a_minimum_size_broadcast_with_the_experimental_ethertype) {
    const mac_address src{{0x7e, 0x12, 0x06, 0x85, 0x13, 0xaf}};
    const auto frame = make_plc_keepalive_frame(src);
    ASSERT_EQ(frame.size(), 60u);
    for (std::size_t i = 0; i < 6; ++i) {
        EXPECT_EQ(frame[i], 0xFF) << "dst byte " << i;
        EXPECT_EQ(frame[6 + i], src[i]) << "src byte " << i;
    }
    EXPECT_EQ(frame[12], 0x88);
    EXPECT_EQ(frame[13], 0xB5);
    EXPECT_EQ(std::string(reinterpret_cast<char const*>(frame.data() + 14), 16), "CB-PLC-KEEPALIVE");
    for (std::size_t i = 30; i < frame.size(); ++i) {
        EXPECT_EQ(frame[i], 0) << "pad byte " << i;
    }
}

TEST(plc_keepalive, parses_the_sysfs_address_format) {
    mac_address mac{};
    ASSERT_TRUE(parse_mac_address("7e:12:06:85:13:af", mac));
    EXPECT_EQ(mac, (mac_address{{0x7e, 0x12, 0x06, 0x85, 0x13, 0xaf}}));
    ASSERT_TRUE(parse_mac_address("7e:12:06:85:13:af\n", mac)); // sysfs has a trailing newline
    EXPECT_FALSE(parse_mac_address("7e:12:06:85:13", mac));
    EXPECT_FALSE(parse_mac_address("7e:12:06:85:13:af:00", mac));
    EXPECT_FALSE(parse_mac_address("zz:12:06:85:13:af", mac));
}

} // namespace
