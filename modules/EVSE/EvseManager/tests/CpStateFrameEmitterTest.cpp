// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <CpStateFrameEmitter.hpp>
#include <gtest/gtest.h>

namespace {

using module::build_cp_state_frame;
using module::MacAddress;
using module::RawCPState;

const MacAddress source{0x02, 0x11, 0x22, 0x33, 0x44, 0x55};

// Offsets into the Ethernet frame, matching Wireshark's homeplug-av dissector and dsV2Gshark's v2ghpscs.lua
constexpr std::size_t offset_ethertype{12};
constexpr std::size_t offset_mmv{14};
constexpr std::size_t offset_mmtype{15};
constexpr std::size_t offset_oui{19};
constexpr std::size_t offset_cp_state{27};
constexpr std::size_t offset_duty_cycle{28};
constexpr std::size_t offset_frequency{29};
constexpr std::size_t offset_voltage{31};

std::uint16_t le16(const std::vector<std::uint8_t>& frame, std::size_t offset) {
    return static_cast<std::uint16_t>(frame[offset] | (frame[offset + 1] << 8));
}

TEST(CpStateFrame, HeaderMatchesIotechaStpCpstateInd) {
    const auto frame = build_cp_state_frame(source, RawCPState::A, 100.0);

    ASSERT_EQ(frame.size(), 60u);
    EXPECT_EQ((MacAddress{0x00, 0xB0, 0x52, 0x00, 0x00, 0x01}),
              (MacAddress{frame[0], frame[1], frame[2], frame[3], frame[4], frame[5]}));
    EXPECT_EQ(source, (MacAddress{frame[6], frame[7], frame[8], frame[9], frame[10], frame[11]}));
    EXPECT_EQ(frame[offset_ethertype], 0x88);
    EXPECT_EQ(frame[offset_ethertype + 1], 0xE1);
    EXPECT_EQ(frame[offset_mmv], 0x01);
    EXPECT_EQ(le16(frame, offset_mmtype), 0xA22E);
    EXPECT_EQ(le16(frame, offset_mmtype + 2), 0x0000);
    EXPECT_EQ(frame[offset_oui], 0x00);
    EXPECT_EQ(frame[offset_oui + 1], 0x80);
    EXPECT_EQ(frame[offset_oui + 2], 0xE1);
}

TEST(CpStateFrame, StateAWithoutPwm) {
    const auto frame = build_cp_state_frame(source, RawCPState::A, 100.0);

    EXPECT_EQ(frame[offset_cp_state], 0x01);
    EXPECT_EQ(frame[offset_duty_cycle], 100);
    EXPECT_EQ(le16(frame, offset_frequency), 0);
    EXPECT_EQ(le16(frame, offset_voltage), 12000);
}

TEST(CpStateFrame, StateBWithFivePercentPwm) {
    const auto frame = build_cp_state_frame(source, RawCPState::B, 5.0);

    EXPECT_EQ(frame[offset_cp_state], 0x03);
    EXPECT_EQ(frame[offset_duty_cycle], 5);
    EXPECT_EQ(le16(frame, offset_frequency), 1000);
    EXPECT_EQ(le16(frame, offset_voltage), 9000);
}

TEST(CpStateFrame, StateCodesAndVoltages) {
    EXPECT_EQ(build_cp_state_frame(source, RawCPState::C, 100.0)[offset_cp_state], 0x05);
    EXPECT_EQ(le16(build_cp_state_frame(source, RawCPState::C, 100.0), offset_voltage), 6000);
    EXPECT_EQ(build_cp_state_frame(source, RawCPState::D, 100.0)[offset_cp_state], 0x07);
    EXPECT_EQ(le16(build_cp_state_frame(source, RawCPState::D, 100.0), offset_voltage), 3000);
    EXPECT_EQ(build_cp_state_frame(source, RawCPState::E, 100.0)[offset_cp_state], 0x09);
    EXPECT_EQ(le16(build_cp_state_frame(source, RawCPState::E, 100.0), offset_voltage), 0);
    EXPECT_EQ(build_cp_state_frame(source, RawCPState::F, 100.0)[offset_cp_state], 0x0A);
    EXPECT_EQ(le16(build_cp_state_frame(source, RawCPState::F, 100.0), offset_voltage), 0);
    EXPECT_EQ(build_cp_state_frame(source, RawCPState::Disabled, 100.0)[offset_cp_state], 0x00);
}

TEST(CpStateFrame, DutyCycleIsRoundedAndClamped) {
    EXPECT_EQ(build_cp_state_frame(source, RawCPState::C, 26.6)[offset_duty_cycle], 27);
    EXPECT_EQ(build_cp_state_frame(source, RawCPState::C, 0.0)[offset_duty_cycle], 0);
    EXPECT_EQ(le16(build_cp_state_frame(source, RawCPState::C, 0.0), offset_frequency), 0);
    EXPECT_EQ(build_cp_state_frame(source, RawCPState::C, 150.0)[offset_duty_cycle], 100);
    EXPECT_EQ(le16(build_cp_state_frame(source, RawCPState::C, 150.0), offset_frequency), 0);
}

TEST(CpStateFrame, PaddingIsZero) {
    const auto frame = build_cp_state_frame(source, RawCPState::B, 5.0);
    for (std::size_t i = offset_voltage + 2; i < frame.size(); ++i) {
        EXPECT_EQ(frame[i], 0x00) << "offset " << i;
    }
}

} // namespace
