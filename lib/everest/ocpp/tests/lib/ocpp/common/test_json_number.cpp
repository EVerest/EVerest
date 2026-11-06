// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

#include <nlohmann/json.hpp>

#include <ocpp/common/json_number.hpp>

using nlohmann::json;

namespace ocpp {

TEST(JsonNumberTest, IntegerWithinRange) {
    EXPECT_EQ(integer_from_json(json::parse("0")), 0);
    EXPECT_EQ(integer_from_json(json::parse("1234")), 1234);
    EXPECT_EQ(integer_from_json(json::parse("-1234")), -1234);
    EXPECT_EQ(integer_from_json(json::parse("2147483647")), std::numeric_limits<std::int32_t>::max());
    EXPECT_EQ(integer_from_json(json::parse("-2147483648")), std::numeric_limits<std::int32_t>::min());
}

TEST(JsonNumberTest, IntegerOutOfRange) {
    EXPECT_THROW(integer_from_json(json::parse("2147483648")), json::out_of_range);
    EXPECT_THROW(integer_from_json(json::parse("-2147483649")), json::out_of_range);
    EXPECT_THROW(integer_from_json(json::parse("18446744073709551615")), json::out_of_range);
    EXPECT_THROW(integer_from_json(json::parse("-9223372036854775808")), json::out_of_range);
}

TEST(JsonNumberTest, IntegerRejectsOtherTypes) {
    // literals beyond the uint64 range are parsed as floating point numbers
    EXPECT_THROW(integer_from_json(json::parse("18446744073709551616")), json::type_error);
    EXPECT_THROW(integer_from_json(json::parse("1e30")), json::type_error);
    EXPECT_THROW(integer_from_json(json::parse("1.0")), json::type_error);
    EXPECT_THROW(integer_from_json(json::parse("1.5")), json::type_error);
    EXPECT_THROW(integer_from_json(json::parse("\"1\"")), json::type_error);
    EXPECT_THROW(integer_from_json(json::parse("true")), json::type_error);
    EXPECT_THROW(integer_from_json(json::parse("null")), json::type_error);
}

// OCPP 2.0.1 and 2.1 Part 2, Table 1 forbid leading zeros and a plus sign for integers. RFC 8259 forbids both for all
// JSON numbers, so the JSON parser already rejects such a message before integer_from_json is reached.
TEST(JsonNumberTest, IntegerWithLeadingZeroOrPlusSignIsNotValidJson) {
    EXPECT_FALSE(json::accept("01234"));
    EXPECT_FALSE(json::accept("-01234"));
    EXPECT_FALSE(json::accept("00"));
    EXPECT_FALSE(json::accept("+1234"));
    EXPECT_FALSE(json::accept("+0"));
    EXPECT_FALSE(json::accept(R"({"connectorId": 01234})"));
    EXPECT_FALSE(json::accept(R"({"connectorId": +1234})"));
    EXPECT_FALSE(json::accept(R"([2, "19223201", "ChangeAvailability", {"connectorId": 01, "type": "Operative"}])"));

    EXPECT_EQ(integer_from_json(json::parse("0")), 0);
    EXPECT_EQ(integer_from_json(json::parse("-0")), 0);
    EXPECT_EQ(integer_from_json(json::parse("1234")), 1234);
    EXPECT_EQ(integer_from_json(json::parse("-1234")), -1234);
}

TEST(JsonNumberTest, DecimalWithinRange) {
    EXPECT_FLOAT_EQ(decimal_from_json(json::parse("0")), 0.0F);
    EXPECT_FLOAT_EQ(decimal_from_json(json::parse("16")), 16.0F);
    EXPECT_FLOAT_EQ(decimal_from_json(json::parse("-7.123456")), -7.123456F);
    EXPECT_FLOAT_EQ(decimal_from_json(json::parse("18446744073709551616")), 18446744073709551616.0F);
    EXPECT_FLOAT_EQ(decimal_from_json(json(std::numeric_limits<float>::max())), std::numeric_limits<float>::max());
    EXPECT_FLOAT_EQ(decimal_from_json(json(std::numeric_limits<float>::lowest())),
                    std::numeric_limits<float>::lowest());
}

TEST(JsonNumberTest, DecimalOutOfRange) {
    EXPECT_THROW(decimal_from_json(json::parse("1e39")), json::out_of_range);
    EXPECT_THROW(decimal_from_json(json::parse("-1e39")), json::out_of_range);
    EXPECT_THROW(decimal_from_json(json(std::numeric_limits<double>::infinity())), json::out_of_range);
    EXPECT_THROW(decimal_from_json(json(std::numeric_limits<double>::quiet_NaN())), json::out_of_range);
}

TEST(JsonNumberTest, DecimalRejectsOtherTypes) {
    EXPECT_THROW(decimal_from_json(json::parse("\"1.5\"")), json::type_error);
    EXPECT_THROW(decimal_from_json(json::parse("false")), json::type_error);
    EXPECT_THROW(decimal_from_json(json::parse("[1.5]")), json::type_error);
}

} // namespace ocpp
