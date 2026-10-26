// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include <everest/crc/crc.hpp>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <random>
#include <set>
#include <vector>

namespace {

// Remainder of (message * x^16) mod (x^16 + x^12 + x^5 + 1), computed by long division over the augmented message.
std::uint16_t reference_crc(const std::vector<std::uint8_t>& message) {
    std::uint32_t remainder = 0;
    const auto shift_in = [&remainder](bool bit) {
        remainder = (remainder << 1) | static_cast<std::uint32_t>(bit);
        if ((remainder & 0x10000) != 0) {
            remainder ^= 0x11021;
        }
    };
    for (const auto byte : message) {
        for (int bit = 7; bit >= 0; --bit) {
            shift_in(((byte >> bit) & 1) != 0);
        }
    }
    for (int i = 0; i < 16; ++i) {
        shift_in(false);
    }
    return static_cast<std::uint16_t>(remainder);
}

std::vector<std::uint8_t> random_message(std::mt19937& rng, std::size_t length) {
    std::vector<std::uint8_t> message(length);
    for (auto& byte : message) {
        byte = static_cast<std::uint8_t>(rng() & 0xFF);
    }
    return message;
}

} // namespace

TEST(CRC16_XModem, KnownVector) {
    std::vector<std::uint8_t> data{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT_EQ(calculate_xModem_crc16(data), 0x31C3);
}

TEST(CRC16_XModem, EmptyInput) {
    std::vector<std::uint8_t> data{};
    EXPECT_EQ(calculate_xModem_crc16(data), 0x0000);
}

TEST(CRC16_XModem, MultipleBytes) {
    std::vector<std::uint8_t> data{'E', 'V', 'E', 'R', 'E', 'S', 'T'};
    EXPECT_EQ(calculate_xModem_crc16(data), 0x4492);
}

TEST(CRC16_XModem, SingleByte) {
    EXPECT_EQ(calculate_xModem_crc16({'A'}), 0x58E5);
    EXPECT_EQ(calculate_xModem_crc16({0x00}), 0x0000);
    EXPECT_EQ(calculate_xModem_crc16({0x01}), 0x1021);
    EXPECT_EQ(calculate_xModem_crc16({0x80}), 0x9188);
    EXPECT_EQ(calculate_xModem_crc16({0xFF}), 0x1EF0);
}

TEST(CRC16_XModem, TwoBytes) {
    EXPECT_EQ(calculate_xModem_crc16({'A', 'B'}), 0x567B);
}

// A frame consisting of a non-zero address byte followed by CRC bytes 0x00 0x00 must never verify.
TEST(CRC16_XModem, SingleBytesHaveDistinctNonZeroCrcs) {
    std::set<std::uint16_t> seen{};
    for (int value = 1; value <= 0xFF; ++value) {
        const auto crc = calculate_xModem_crc16({static_cast<std::uint8_t>(value)});
        EXPECT_NE(crc, 0x0000) << "byte " << value;
        EXPECT_TRUE(seen.insert(crc).second) << "byte " << value;
    }
}

TEST(CRC16_XModem, MatchesReferenceForAllSingleAndTwoByteMessages) {
    for (int first = 0; first <= 0xFF; ++first) {
        const std::vector<std::uint8_t> single{static_cast<std::uint8_t>(first)};
        ASSERT_EQ(calculate_xModem_crc16(single), reference_crc(single)) << "byte " << first;
        for (int second = 0; second <= 0xFF; ++second) {
            const std::vector<std::uint8_t> pair{static_cast<std::uint8_t>(first), static_cast<std::uint8_t>(second)};
            ASSERT_EQ(calculate_xModem_crc16(pair), reference_crc(pair)) << "bytes " << first << ", " << second;
        }
    }
}

TEST(CRC16_XModem, MatchesReferenceForRandomMessagesOfEveryLength) {
    std::mt19937 rng{0xC0FFEE};
    for (std::size_t length = 0; length <= 1500; ++length) {
        const auto message = random_message(rng, length);
        ASSERT_EQ(calculate_xModem_crc16(message), reference_crc(message)) << "length " << length;
    }
}

TEST(CRC16_XModem, AppendedCrcLeavesZeroRemainder) {
    std::mt19937 rng{0x5EED};
    for (std::size_t length = 0; length <= 64; ++length) {
        auto message = random_message(rng, length);
        const auto crc = calculate_xModem_crc16(message);
        message.push_back(static_cast<std::uint8_t>(crc >> 8));
        message.push_back(static_cast<std::uint8_t>(crc & 0xFF));
        EXPECT_EQ(calculate_xModem_crc16(message), 0x0000) << "length " << length;
    }
}
