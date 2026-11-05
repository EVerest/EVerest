// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>
#include <ocpp/common/types.hpp>

namespace ocpp {

TEST(DateTimeTest, parses_rfc3339) {
    EXPECT_EQ(DateTime("2024-06-01T12:00:00Z").to_rfc3339(), "2024-06-01T12:00:00.000Z");
    EXPECT_EQ(DateTime("2024-06-01T14:00:00.5+02:00").to_rfc3339(), "2024-06-01T12:00:00.500Z");
    EXPECT_EQ(DateTime("2016-12-31T23:59:60Z").to_rfc3339(), "2016-12-31T23:59:60.000Z");
}

TEST(DateTimeTest, parses_iso8601) {
    EXPECT_EQ(DateTime("2024-06-01T12:00:00").to_rfc3339(), "2024-06-01T12:00:00.000Z");
    EXPECT_EQ(DateTime("2024-06-01T12:00:00,25Z").to_rfc3339(), "2024-06-01T12:00:00.250Z");
    EXPECT_EQ(DateTime("2024-06-01T17:30:00+0530").to_rfc3339(), "2024-06-01T12:00:00.000Z");
    EXPECT_EQ(DateTime("2024-06-01T14:00:00+02").to_rfc3339(), "2024-06-01T12:00:00.000Z");
}

TEST(DateTimeTest, keeps_a_long_fraction_out_of_the_utc_offset) {
    EXPECT_EQ(DateTime("2024-06-01T12:00:00.1234567891Z").to_rfc3339(), "2024-06-01T12:00:00.123Z");
}

TEST(DateTimeTest, round_trips_the_limits_of_utc_clock) {
    for (const auto& timepoint : {date::utc_clock::time_point::min(), date::utc_clock::time_point::max()}) {
        const auto timepoint_str = DateTime(timepoint).to_rfc3339();
        EXPECT_EQ(DateTime(timepoint_str).to_rfc3339(), timepoint_str);
    }
}

TEST(DateTimeTest, clamps_dates_outside_the_utc_clock_range) {
    for (const auto* timepoint_str :
         {"0000-01-01T00:00:00Z", "1600-01-01T00:00:00Z", "1677-09-21T00:00:00Z", "1677-09-21T00:00:00+00:01"}) {
        EXPECT_EQ(DateTime(timepoint_str).to_time_point(), date::utc_clock::time_point::min()) << timepoint_str;
    }
    for (const auto* timepoint_str :
         {"2262-04-12T00:00:00Z", "2262-04-11T00:00:00-23:59", "2300-01-01T00:00:00", "9999-12-31T23:59:59Z"}) {
        EXPECT_EQ(DateTime(timepoint_str).to_time_point(), date::utc_clock::time_point::max()) << timepoint_str;
    }
}

TEST(DateTimeTest, rejects_invalid_date_times) {
    for (const auto* timepoint_str :
         {"", "garbage", "2026-02-30T00:00:00Z", "2026-01-02T23:59:60Z", "2026-01-02T03:04:05Z garbage",
          "2026-01-02T03:04:05ZZ", "2026-01-02T03:04:05+24:00", "2026-01-02T03:04:05+02:60", "2026-01-02T03:04:05+2",
          "2026-01-02T03:04:05+2:00", "2026-01-02T03:04:05+02:0", "2026-01-02T03:04:05.Z",
          "2026-01-02T03:04:09999999995", "2026-01-02T003:04:05Z", "02026-01-02T03:04:05Z", "26-1-2T3:4:5Z"}) {
        EXPECT_THROW(DateTime{timepoint_str}, TimePointParseException) << timepoint_str;
    }
}

} // namespace ocpp
