// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <cstdint>

#include <cbv2g/common/exi_basetypes_decoder.h>
#include <cbv2g/common/exi_basetypes_encoder.h>
#include <cbv2g/common/exi_bitstream.h>
#include <cbv2g/common/exi_error_codes.h>

// An EXI integer is a sign bit followed by an unsigned magnitude; a negative value is -(magnitude) - 1.

namespace {

template <typename T> struct Decoded {
    int error;
    T value;
};

template <typename T>
Decoded<T> decode_sign_and_magnitude(int (*decode)(exi_bitstream_t*, T*), bool negative, uint64_t magnitude) {
    uint8_t data[16] = {};
    exi_bitstream_t stream;
    exi_bitstream_init(&stream, data, sizeof(data), 0, nullptr);

    REQUIRE(exi_basetypes_encoder_bool(&stream, negative) == EXI_ERROR__NO_ERROR);
    REQUIRE(exi_basetypes_encoder_uint_64(&stream, magnitude) == EXI_ERROR__NO_ERROR);

    exi_bitstream_reset(&stream);
    Decoded<T> result{};
    result.error = decode(&stream, &result.value);
    return result;
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

        THEN("It does not fit the destination") {
            CHECK(result.error == EXI_ERROR__ENCODED_INTEGER_SIZE_LARGER_THAN_DESTINATION);
        }
    }

    GIVEN("The sign clear and magnitude 128") {
        const auto result = decode_sign_and_magnitude(exi_basetypes_decoder_integer_8, false, 128);

        THEN("It does not fit the destination") {
            CHECK(result.error == EXI_ERROR__ENCODED_INTEGER_SIZE_LARGER_THAN_DESTINATION);
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

        THEN("It does not fit the destination") {
            CHECK(result.error == EXI_ERROR__ENCODED_INTEGER_SIZE_LARGER_THAN_DESTINATION);
        }
    }

    GIVEN("The sign clear and magnitude 32768") {
        const auto result = decode_sign_and_magnitude(exi_basetypes_decoder_integer_16, false, 32768);

        THEN("It does not fit the destination") {
            CHECK(result.error == EXI_ERROR__ENCODED_INTEGER_SIZE_LARGER_THAN_DESTINATION);
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

        THEN("It does not fit the destination") {
            CHECK(result.error == EXI_ERROR__ENCODED_INTEGER_SIZE_LARGER_THAN_DESTINATION);
        }
    }

    GIVEN("The sign set and magnitude 2^31") {
        const auto result = decode_sign_and_magnitude(exi_basetypes_decoder_integer_32, true, one_past_int32);

        THEN("It does not fit the destination") {
            CHECK(result.error == EXI_ERROR__ENCODED_INTEGER_SIZE_LARGER_THAN_DESTINATION);
        }
    }
}
