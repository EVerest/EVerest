/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2026 Contributors to EVerest */
//
// The bitstream stops at the end of the stream, in both directions.
//
// exi_bitstream_has_overflow() used to advance byte_pos only when bit_count reached a full byte,
// and never checked the byte it was about to touch. One byte past the end therefore passed the
// guard, and write_bit and read_bit dereferenced it. A well-formed document never reaches it; a
// truncated or oversized one always does, and the EV Plug and Charge path decodes a peer's
// CertificateInstallationRes out of an exactly-sized vector, where the byte past the end is
// someone else's.
//
// Both cases below are checked without a sanitiser: the stream is told it owns fewer bytes than
// the array actually holds, so the trailing guard bytes are a canary for the write and a distinct
// value for the read.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

#include <cbv2g/common/exi_bitstream.h>
#include <cbv2g/common/exi_error_codes.h>

namespace {

constexpr size_t STREAM_BYTES = 8;
constexpr size_t GUARD_BYTES = 8;
constexpr uint8_t GUARD_VALUE = 0xA5;

} // namespace

SCENARIO("Writing past the end of the stream fails without touching the next byte") {
    std::array<uint8_t, STREAM_BYTES + GUARD_BYTES> storage{};
    storage.fill(GUARD_VALUE);

    exi_bitstream_t stream;
    exi_bitstream_init(&stream, storage.data(), STREAM_BYTES, 0, nullptr);

    GIVEN("A stream filled to capacity") {
        for (size_t i = 0; i < STREAM_BYTES; ++i) {
            REQUIRE(exi_bitstream_write_octet(&stream, 0x5Cu) == EXI_ERROR__NO_ERROR);
        }

        THEN("The next octet is refused") {
            REQUIRE(exi_bitstream_write_octet(&stream, 0x5Cu) == EXI_ERROR__BITSTREAM_OVERFLOW);
        }

        THEN("The bytes after the stream are untouched") {
            (void)exi_bitstream_write_octet(&stream, 0x5Cu);
            for (size_t i = STREAM_BYTES; i < storage.size(); ++i) {
                INFO("guard byte " << i);
                REQUIRE(storage[i] == GUARD_VALUE);
            }
        }

        THEN("The reported length never exceeds the stream") {
            (void)exi_bitstream_write_octet(&stream, 0x5Cu);
            REQUIRE(exi_bitstream_get_length(&stream) <= STREAM_BYTES);
        }
    }
}

SCENARIO("Reading past the end of the stream fails without reading the next byte") {
    std::array<uint8_t, STREAM_BYTES + GUARD_BYTES> storage{};
    storage.fill(GUARD_VALUE);

    exi_bitstream_t stream;
    exi_bitstream_init(&stream, storage.data(), STREAM_BYTES, 0, nullptr);

    GIVEN("A stream read to its end") {
        uint8_t value = 0;
        for (size_t i = 0; i < STREAM_BYTES; ++i) {
            REQUIRE(exi_bitstream_read_octet(&stream, &value) == EXI_ERROR__NO_ERROR);
        }

        THEN("The next octet is refused rather than read from beyond the end") {
            value = 0;
            REQUIRE(exi_bitstream_read_octet(&stream, &value) == EXI_ERROR__BITSTREAM_OVERFLOW);
            REQUIRE(value != GUARD_VALUE);
        }
    }
}
