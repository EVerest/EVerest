// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <initializer_list>
#include <string>

#include <cbv2g/common/exi_basetypes.h>
#include <cbv2g/common/exi_basetypes_decoder.h>
#include <cbv2g/common/exi_basetypes_encoder.h>
#include <cbv2g/common/exi_bitstream.h>
#include <cbv2g/common/exi_error_codes.h>

// An EXI integer is a sign bit followed by an unsigned magnitude; a negative value is -(magnitude) - 1.

namespace {

// Every decoder under test is handed this value and must leave it in place when it fails.
constexpr int destination_sentinel = 42;

template <typename T> struct Decoded {
    int error;
    T value;
};

template <typename T, typename Write> Decoded<T> decode_written(int (*decode)(exi_bitstream_t*, T*), Write write) {
    uint8_t data[16] = {};
    exi_bitstream_t stream;
    exi_bitstream_init(&stream, data, sizeof(data), 0, nullptr);

    write(&stream);

    exi_bitstream_reset(&stream);
    Decoded<T> result{EXI_ERROR__NO_ERROR, static_cast<T>(destination_sentinel)};
    result.error = decode(&stream, &result.value);
    return result;
}

template <typename T>
Decoded<T> decode_sign_and_magnitude(int (*decode)(exi_bitstream_t*, T*), bool negative, uint64_t magnitude) {
    return decode_written(decode, [&](exi_bitstream_t* stream) {
        REQUIRE(exi_basetypes_encoder_bool(stream, negative) == EXI_ERROR__NO_ERROR);
        REQUIRE(exi_basetypes_encoder_uint_64(stream, magnitude) == EXI_ERROR__NO_ERROR);
    });
}

template <typename T> Decoded<T> decode_unsigned(int (*decode)(exi_bitstream_t*, T*), uint64_t value) {
    return decode_written(decode, [&](exi_bitstream_t* stream) {
        REQUIRE(exi_basetypes_encoder_uint_64(stream, value) == EXI_ERROR__NO_ERROR);
    });
}

template <typename T>
Decoded<T> decode_octets(int (*decode)(exi_bitstream_t*, T*), std::initializer_list<uint8_t> octets) {
    return decode_written(decode, [&](exi_bitstream_t* stream) {
        for (const auto octet : octets) {
            REQUIRE(exi_basetypes_encoder_nbit_uint(stream, 8, octet) == EXI_ERROR__NO_ERROR);
        }
    });
}

struct SignAndMagnitude {
    int sign;
    uint64_t magnitude;
};

template <typename T>
void check_round_trip(int (*encode)(exi_bitstream_t*, T), int (*decode)(exi_bitstream_t*, T*), T value,
                      SignAndMagnitude expected) {
    uint8_t data[16] = {};
    exi_bitstream_t stream;
    exi_bitstream_init(&stream, data, sizeof(data), 0, nullptr);

    REQUIRE(encode(&stream, value) == EXI_ERROR__NO_ERROR);

    exi_bitstream_reset(&stream);
    SignAndMagnitude encoded{};
    REQUIRE(exi_basetypes_decoder_bool(&stream, &encoded.sign) == EXI_ERROR__NO_ERROR);
    REQUIRE(exi_basetypes_decoder_uint_64(&stream, &encoded.magnitude) == EXI_ERROR__NO_ERROR);
    CHECK(encoded.sign == expected.sign);
    CHECK(encoded.magnitude == expected.magnitude);

    exi_bitstream_reset(&stream);
    T decoded{};
    REQUIRE(decode(&stream, &decoded) == EXI_ERROR__NO_ERROR);
    CHECK(decoded == value);
}

} // namespace

SCENARIO("Decode signed 8 bit integers at the edge of the range") {
    GIVEN("The sign set and magnitude 127") {
        const auto result = decode_sign_and_magnitude(exi_basetypes_decoder_integer_8, true, 127);

        THEN("It decodes to INT8_MIN") {
            REQUIRE(result.error == EXI_ERROR__NO_ERROR);
            CHECK(result.value == INT8_MIN);
        }
    }

    GIVEN("The sign set and magnitude 128") {
        const auto result = decode_sign_and_magnitude(exi_basetypes_decoder_integer_8, true, 128);

        THEN("It does not fit the destination and leaves it unchanged") {
            CHECK(result.error == EXI_ERROR__ENCODED_INTEGER_SIZE_LARGER_THAN_DESTINATION);
            CHECK(result.value == destination_sentinel);
        }
    }

    GIVEN("The sign clear and magnitude 128") {
        const auto result = decode_sign_and_magnitude(exi_basetypes_decoder_integer_8, false, 128);

        THEN("It does not fit the destination and leaves it unchanged") {
            CHECK(result.error == EXI_ERROR__ENCODED_INTEGER_SIZE_LARGER_THAN_DESTINATION);
            CHECK(result.value == destination_sentinel);
        }
    }
}

SCENARIO("Decode signed 16 bit integers at the edge of the range") {
    GIVEN("The sign set and magnitude 32767") {
        const auto result = decode_sign_and_magnitude(exi_basetypes_decoder_integer_16, true, 32767);

        THEN("It decodes to INT16_MIN") {
            REQUIRE(result.error == EXI_ERROR__NO_ERROR);
            CHECK(result.value == INT16_MIN);
        }
    }

    GIVEN("The sign set and magnitude 32768") {
        const auto result = decode_sign_and_magnitude(exi_basetypes_decoder_integer_16, true, 32768);

        THEN("It does not fit the destination and leaves it unchanged") {
            CHECK(result.error == EXI_ERROR__ENCODED_INTEGER_SIZE_LARGER_THAN_DESTINATION);
            CHECK(result.value == destination_sentinel);
        }
    }

    GIVEN("The sign clear and magnitude 32768") {
        const auto result = decode_sign_and_magnitude(exi_basetypes_decoder_integer_16, false, 32768);

        THEN("It does not fit the destination and leaves it unchanged") {
            CHECK(result.error == EXI_ERROR__ENCODED_INTEGER_SIZE_LARGER_THAN_DESTINATION);
            CHECK(result.value == destination_sentinel);
        }
    }
}

SCENARIO("Encode and decode signed 32 and 64 bit integers") {
    GIVEN("32 bit values") {
        THEN("INT32_MIN encodes as the sign and magnitude INT32_MAX and decodes back") {
            check_round_trip<int32_t>(exi_basetypes_encoder_integer_32, exi_basetypes_decoder_integer_32, INT32_MIN,
                                      {1, INT32_MAX});
        }
        THEN("INT32_MAX encodes as magnitude INT32_MAX and decodes back") {
            check_round_trip<int32_t>(exi_basetypes_encoder_integer_32, exi_basetypes_decoder_integer_32, INT32_MAX,
                                      {0, INT32_MAX});
        }
        THEN("-1 encodes as the sign and magnitude 0 and decodes back") {
            check_round_trip<int32_t>(exi_basetypes_encoder_integer_32, exi_basetypes_decoder_integer_32, -1, {1, 0});
        }
        THEN("0 encodes as magnitude 0 and decodes back") {
            check_round_trip<int32_t>(exi_basetypes_encoder_integer_32, exi_basetypes_decoder_integer_32, 0, {0, 0});
        }
    }

    GIVEN("64 bit values") {
        THEN("INT64_MIN encodes as the sign and magnitude INT64_MAX and decodes back") {
            check_round_trip<int64_t>(exi_basetypes_encoder_integer_64, exi_basetypes_decoder_integer_64, INT64_MIN,
                                      {1, INT64_MAX});
        }
        THEN("INT64_MAX encodes as magnitude INT64_MAX and decodes back") {
            check_round_trip<int64_t>(exi_basetypes_encoder_integer_64, exi_basetypes_decoder_integer_64, INT64_MAX,
                                      {0, INT64_MAX});
        }
        THEN("-1 encodes as the sign and magnitude 0 and decodes back") {
            check_round_trip<int64_t>(exi_basetypes_encoder_integer_64, exi_basetypes_decoder_integer_64, -1, {1, 0});
        }
        THEN("0 encodes as magnitude 0 and decodes back") {
            check_round_trip<int64_t>(exi_basetypes_encoder_integer_64, exi_basetypes_decoder_integer_64, 0, {0, 0});
        }
    }
}

SCENARIO("Decode a signed 32 bit integer whose magnitude does not fit") {
    const uint64_t one_past_int32 = static_cast<uint64_t>(INT32_MAX) + 1;

    GIVEN("The sign clear and magnitude 2^31") {
        const auto result = decode_sign_and_magnitude(exi_basetypes_decoder_integer_32, false, one_past_int32);

        THEN("It does not fit the destination and leaves it unchanged") {
            CHECK(result.error == EXI_ERROR__ENCODED_INTEGER_SIZE_LARGER_THAN_DESTINATION);
            CHECK(result.value == destination_sentinel);
        }
    }

    GIVEN("The sign set and magnitude 2^31") {
        const auto result = decode_sign_and_magnitude(exi_basetypes_decoder_integer_32, true, one_past_int32);

        THEN("It does not fit the destination and leaves it unchanged") {
            CHECK(result.error == EXI_ERROR__ENCODED_INTEGER_SIZE_LARGER_THAN_DESTINATION);
            CHECK(result.value == destination_sentinel);
        }
    }
}

SCENARIO("Decode unsigned integers one past the destination's range") {
    GIVEN("The value 256") {
        const auto result = decode_unsigned(exi_basetypes_decoder_uint_8, uint64_t{UINT8_MAX} + 1);

        THEN("The 8 bit decoder rejects it and leaves the destination unchanged") {
            CHECK(result.error == EXI_ERROR__ENCODED_INTEGER_SIZE_LARGER_THAN_DESTINATION);
            CHECK(result.value == destination_sentinel);
        }
    }

    GIVEN("The value 65536") {
        const auto result = decode_unsigned(exi_basetypes_decoder_uint_16, uint64_t{UINT16_MAX} + 1);

        THEN("The 16 bit decoder rejects it and leaves the destination unchanged") {
            CHECK(result.error == EXI_ERROR__ENCODED_INTEGER_SIZE_LARGER_THAN_DESTINATION);
            CHECK(result.value == destination_sentinel);
        }
    }

    GIVEN("The value 2^32") {
        const auto result = decode_unsigned(exi_basetypes_decoder_uint_32, uint64_t{UINT32_MAX} + 1);

        THEN("The 32 bit decoder rejects it and leaves the destination unchanged") {
            CHECK(result.error == EXI_ERROR__ENCODED_INTEGER_SIZE_LARGER_THAN_DESTINATION);
            CHECK(result.value == destination_sentinel);
        }
    }
}

SCENARIO("Regression pin: decode an unsigned 32 bit integer spread over more than five octets") {
    GIVEN("The value 1 padded to six octets with continuation flags") {
        const auto result = decode_octets(exi_basetypes_decoder_uint_32, {0x81, 0x80, 0x80, 0x80, 0x80, 0x00});

        THEN("The 32 bit decoder rejects it and leaves the destination unchanged") {
            CHECK(result.error == EXI_ERROR__OCTET_COUNT_LARGER_THAN_TYPE_SUPPORTS);
            CHECK(result.value == destination_sentinel);
        }
    }
}

SCENARIO("Convert 64 bit integers to and from exi_signed_t through the signed coders") {
    for (const int64_t value : {int64_t{12345}, int64_t{-12345}}) {
        GIVEN("The value " + std::to_string(value)) {
            THEN("Converted and written with the signed encoder, it decodes as the same 64 bit integer") {
                const auto result = decode_written(exi_basetypes_decoder_integer_64, [&](exi_bitstream_t* stream) {
                    exi_signed_t converted{};
                    REQUIRE(exi_basetypes_convert_64_to_signed(&converted, value) == EXI_ERROR__NO_ERROR);
                    REQUIRE(exi_basetypes_encoder_signed(stream, &converted) == EXI_ERROR__NO_ERROR);
                });
                REQUIRE(result.error == EXI_ERROR__NO_ERROR);
                CHECK(result.value == value);
            }

            THEN("Written as a 64 bit integer and read with the signed decoder, it converts back") {
                uint8_t data[16] = {};
                exi_bitstream_t stream;
                exi_bitstream_init(&stream, data, sizeof(data), 0, nullptr);
                REQUIRE(exi_basetypes_encoder_integer_64(&stream, value) == EXI_ERROR__NO_ERROR);

                exi_bitstream_reset(&stream);
                exi_signed_t decoded{};
                REQUIRE(exi_basetypes_decoder_signed(&stream, &decoded) == EXI_ERROR__NO_ERROR);
                int64_t converted = destination_sentinel;
                REQUIRE(exi_basetypes_convert_64_from_signed(&decoded, &converted) == EXI_ERROR__NO_ERROR);
                CHECK(converted == value);
            }
        }
    }
}
