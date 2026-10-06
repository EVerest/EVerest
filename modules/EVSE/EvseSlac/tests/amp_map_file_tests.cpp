// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// The amplitude map limits transmit power; a file that cannot be applied exactly must be rejected as
// a whole, never clamped or partially applied.
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "amp_map_file.hpp"

namespace {

using module::main::amp_map_from_json;
using module::main::AMP_MAP_MAX_AMPLITUDE;
using module::main::AMP_MAP_MAX_CARRIERS;
using module::main::AmpMap;
using module::main::default_amp_map;
using module::main::pack_amp_map;
using json = nlohmann::json;

TEST(AmpMapFile, DefaultMapIsAllMaximumTx) {
    auto const map = default_amp_map();
    EXPECT_EQ(map.len, module::main::AMP_MAP_DEFAULT_CARRIERS);
    ASSERT_EQ(map.data.size(), 578U);
    for (std::size_t i = 0; i + 1 < map.data.size(); ++i) {
        EXPECT_EQ(map.data[i], 0xFF) << "byte " << i;
    }
    // 1155 carriers: the last byte holds carrier 1154 in its low nibble and a zero padding nibble.
    EXPECT_EQ(map.data.back(), 0x0F);
}

TEST(AmpMapFile, PacksTwoCarriersPerByteEvenInLowNibble) {
    auto const map = pack_amp_map(3, 0xF, {{1, 0x3}, {2, 0x0}});
    EXPECT_EQ(map.len, 3);
    ASSERT_EQ(map.data.size(), 2U);
    EXPECT_EQ(map.data[0], 0x3F); // carrier 0 = F (low nibble), carrier 1 = 3 (high nibble)
    EXPECT_EQ(map.data[1], 0x00); // carrier 2 = 0, padding nibble 0
}

TEST(AmpMapFile, AcceptsAValidFile) {
    std::string error;
    auto const map =
        amp_map_from_json(json{{"carriers", 4}, {"default_amplitude", 9}, {"overrides", {{"0", 1}}}}, error);
    ASSERT_TRUE(map.has_value()) << error;
    EXPECT_EQ(map->len, 4);
    ASSERT_EQ(map->data.size(), 2U);
    EXPECT_EQ(map->data[0], 0x91);
    EXPECT_EQ(map->data[1], 0x99);
}

// The cap is what a CM_AMP_MAP.REQ can carry: the largest accepted map frames, the next one does not.
TEST(AmpMapFile, TheCarrierCapIsExactlyWhatOneFrameCarries) {
    EXPECT_EQ(AMP_MAP_MAX_CARRIERS, 2986);
    std::string error;
    auto const map = amp_map_from_json(json{{"carriers", AMP_MAP_MAX_CARRIERS}}, error);
    ASSERT_TRUE(map.has_value()) << error;
    auto const frame = [](AmpMap const& m) {
        std::vector<std::uint8_t> payload;
        payload.push_back(static_cast<std::uint8_t>(m.len & 0xFF));
        payload.push_back(static_cast<std::uint8_t>((m.len >> 8) & 0xFF));
        payload.insert(payload.end(), m.data.begin(), m.data.end());
        everest::lib::slac::messages::HomeplugMessage hp_message;
        hp_message.setup_payload(payload.data(), payload.size(),
                                 everest::lib::slac::defs::MMTYPE_CM_AMP_MAP |
                                     everest::lib::slac::defs::MMTYPE_MODE_REQ,
                                 everest::lib::slac::defs::MMV::AV_2_0);
    };
    EXPECT_NO_THROW(frame(*map));
    auto one_more = pack_amp_map(AMP_MAP_MAX_CARRIERS + 1, AMP_MAP_MAX_AMPLITUDE, {});
    EXPECT_THROW(frame(one_more), std::runtime_error);
}

TEST(AmpMapFile, RejectsEverythingItCannotApplyExactly) {
    std::string error;
    EXPECT_FALSE(amp_map_from_json(json::array(), error).has_value()) << "non-mapping root";
    EXPECT_FALSE(amp_map_from_json(json{{"carrier", 4}}, error).has_value()) << "unknown key";
    EXPECT_NE(error.find("unknown key"), std::string::npos);
    EXPECT_FALSE(amp_map_from_json(json{{"carriers", 0}}, error).has_value()) << "carriers below 1";
    EXPECT_FALSE(amp_map_from_json(json{{"carriers", AMP_MAP_MAX_CARRIERS + 1}}, error).has_value())
        << "carriers above what one frame carries";
    EXPECT_FALSE(amp_map_from_json(json{{"default_amplitude", 16}}, error).has_value()) << "amplitude above 15";
    EXPECT_FALSE(amp_map_from_json(json{{"default_amplitude", "max"}}, error).has_value()) << "amplitude not int";
    EXPECT_FALSE(amp_map_from_json(json{{"overrides", 3}}, error).has_value()) << "overrides not a mapping";
    EXPECT_FALSE(amp_map_from_json(json{{"overrides", {{"x", 1}}}}, error).has_value()) << "non-numeric index";
    EXPECT_FALSE(amp_map_from_json(json{{"carriers", 4}, {"overrides", {{"4", 1}}}}, error).has_value())
        << "index beyond carriers";
    EXPECT_NE(error.find("outside 0..3"), std::string::npos) << error;
    EXPECT_FALSE(amp_map_from_json(json{{"overrides", {{"1", 16}}}}, error).has_value()) << "override above 15";
}

} // namespace
