// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <array>
#include <cstring>

#include <catch2/catch_test_macros.hpp>

#include <cbv2g/common/exi_bitstream.h>
#include <cbv2g/common/exi_error_codes.h>

// The writer must stop at the end of the buffer it was handed. Callers size that buffer from
// exi_bitstream_init, so an encoder that walks past it corrupts whatever the caller put next.
SCENARIO("cbv2g bitstream writer respects the output buffer bound") {

    GIVEN("A four byte output buffer with a guard region immediately after it") {
        struct Framed {
            std::array<uint8_t, 4> buffer;
            std::array<uint8_t, 4> guard;
        };
        Framed framed{};
        framed.buffer.fill(0x00);
        framed.guard.fill(0xAA);

        exi_bitstream_t stream;
        exi_bitstream_init(&stream, framed.buffer.data(), framed.buffer.size(), 0, nullptr);

        WHEN("exactly as many bits as the buffer holds are written") {
            int error = EXI_ERROR__NO_ERROR;
            for (size_t i = 0; i < framed.buffer.size() * 8 and error == EXI_ERROR__NO_ERROR; ++i) {
                error = exi_bitstream_write_bits(&stream, 1, 1);
            }

            THEN("every one of them is accepted, so the bound costs no capacity") {
                REQUIRE(error == EXI_ERROR__NO_ERROR);
                REQUIRE(framed.buffer[3] == 0xFF);
            }

            AND_WHEN("one more bit is written") {
                const auto error_past_end = exi_bitstream_write_bits(&stream, 1, 1);

                THEN("it is reported as an overflow rather than silently accepted") {
                    REQUIRE(error_past_end == EXI_ERROR__BITSTREAM_OVERFLOW);
                }

                THEN("the guard region is untouched") {
                    REQUIRE(framed.guard[0] == 0xAA);
                    REQUIRE(framed.guard[1] == 0xAA);
                    REQUIRE(framed.guard[2] == 0xAA);
                    REQUIRE(framed.guard[3] == 0xAA);
                }

                THEN("the write position never points past the buffer") {
                    REQUIRE(stream.byte_pos < framed.buffer.size());
                }
            }
        }

        WHEN("a multi-bit write straddles the end of the buffer") {
            int error = EXI_ERROR__NO_ERROR;
            for (size_t i = 0; i < 3 and error == EXI_ERROR__NO_ERROR; ++i) {
                error = exi_bitstream_write_bits(&stream, 8, 0xFF);
            }
            REQUIRE(error == EXI_ERROR__NO_ERROR);

            // 8 bits still fit, then a 32 bit write cannot.
            REQUIRE(exi_bitstream_write_bits(&stream, 8, 0xFF) == EXI_ERROR__NO_ERROR);
            const auto error_past_end = exi_bitstream_write_bits(&stream, 32, 0xFFFFFFFF);

            THEN("the overflow is reported and nothing past the buffer is written") {
                REQUIRE(error_past_end == EXI_ERROR__BITSTREAM_OVERFLOW);
                REQUIRE(framed.guard[0] == 0xAA);
            }
        }

        WHEN("a single byte buffer is filled and then overrun") {
            std::array<uint8_t, 1> one{};
            uint8_t guard = 0xAA;
            exi_bitstream_t tight;
            exi_bitstream_init(&tight, one.data(), one.size(), 0, nullptr);

            int error = EXI_ERROR__NO_ERROR;
            for (size_t i = 0; i < 8 and error == EXI_ERROR__NO_ERROR; ++i) {
                error = exi_bitstream_write_bits(&tight, 1, 1);
            }

            THEN("the byte fills, the next bit overflows, and the guard survives") {
                REQUIRE(error == EXI_ERROR__NO_ERROR);
                REQUIRE(one[0] == 0xFF);
                REQUIRE(exi_bitstream_write_bits(&tight, 1, 1) == EXI_ERROR__BITSTREAM_OVERFLOW);
                REQUIRE(guard == 0xAA);
            }
        }
    }
}
