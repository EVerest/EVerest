// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/date/date.hpp>
#include <gtest/gtest.h>

#include <locale>
#include <string>

namespace {

using namespace std::chrono_literals;
using everest::lib::datetime::parse_rfc3339;
using everest::lib::datetime::parse_rfc3339_lenient;
using everest::lib::datetime::parse_rfc3339_lenient_clamped;
using everest::lib::datetime::to_rfc3339;
using everest::lib::datetime::utc_time_point;

utc_time_point utc(date::year_month_day day, std::chrono::nanoseconds time_of_day) {
    return date::utc_clock::from_sys(date::sys_days{day} + time_of_day);
}

const date::year_month_day jan_2{date::year{2026}, date::month{1}, date::day{2}};

// Formats the limits with full precision, they end in ...192Z and ...807Z, so changing the last digit moves them by
// one nanosecond
std::string limit_with_last_digit_moved(utc_time_point limit, int step) {
    auto formatted = date::format("%FT%TZ", limit);
    formatted[formatted.size() - 2] = static_cast<char>(formatted[formatted.size() - 2] + step);
    return formatted;
}

TEST(DateTest, parses_rfc3339) {
    EXPECT_EQ(parse_rfc3339("2026-01-02T03:04:05Z"), utc(jan_2, 11045s));
    EXPECT_EQ(parse_rfc3339("2026-01-02t03:04:05z"), utc(jan_2, 11045s));
    EXPECT_EQ(parse_rfc3339("2026-01-02T03:04:05.678Z"), utc(jan_2, 11045678ms));
    EXPECT_EQ(parse_rfc3339("2026-01-02T03:04:05.123456789Z"), utc(jan_2, 11045s + 123456789ns));
    EXPECT_EQ(parse_rfc3339("2026-01-02T03:04:05.1234567891Z"), utc(jan_2, 11045s + 123456789ns));
    EXPECT_EQ(parse_rfc3339("2026-01-02T05:04:05+02:00"), utc(jan_2, 11045s));
    EXPECT_EQ(parse_rfc3339("2026-01-01T23:34:05.5-03:30"), utc(jan_2, 11045500ms));
    EXPECT_EQ(parse_rfc3339("2026-01-02T03:04:05-00:00"), utc(jan_2, 11045s));
    EXPECT_TRUE(parse_rfc3339("2024-02-29T00:00:00Z").has_value());
}

TEST(DateTest, keeps_a_long_fraction_out_of_the_utc_offset) {
    EXPECT_EQ(parse_rfc3339("2026-01-02T03:04:05.99999999999999999999999999999999999999999Z"),
              utc(jan_2, 11045s + 999999999ns));
    EXPECT_EQ(parse_rfc3339_lenient("2026-01-02T03:04:05.044444444800z"), utc(jan_2, 11045s + 44444444ns));
}

TEST(DateTest, accepts_second_60_only_for_a_leap_second) {
    const date::year_month_day jan_1_2017{date::year{2017}, date::month{1}, date::day{1}};
    EXPECT_EQ(parse_rfc3339("2016-12-31T23:59:60Z"), utc(jan_1_2017, 0s) - 1s);
    EXPECT_EQ(parse_rfc3339("2016-12-31T23:59:60.5Z"), utc(jan_1_2017, 0s) - 500ms);
    EXPECT_EQ(parse_rfc3339("1990-12-31T15:59:60-08:00"),
              utc(date::year_month_day{date::year{1991}, date::month{1}, date::day{1}}, 0s) - 1s);
    EXPECT_EQ(parse_rfc3339("2017-01-01T05:29:60+05:30"), utc(jan_1_2017, 0s) - 1s);
    EXPECT_FALSE(parse_rfc3339("2026-01-02T23:59:60Z").has_value());
    EXPECT_FALSE(parse_rfc3339("2016-12-31T23:59:60+01:00").has_value());
    EXPECT_FALSE(parse_rfc3339_lenient("2016-12-31T23:59:60-3").has_value());
}

TEST(DateTest, rejects_invalid_rfc3339) {
    for (const auto* input : {"",
                              "2026",
                              "2026-01-02",
                              "2026-01-02T03:04",
                              "2026-01-02T03:04:05",
                              "2026-01-02T03:04:05.678",
                              "2026-99-02T03:04:05Z",
                              "2026-00-02T03:04:05Z",
                              "2026-02-30T03:04:05Z",
                              "2025-02-29T03:04:05Z",
                              "2026-01-32T03:04:05Z",
                              "2026-01-02T24:04:05Z",
                              "2026-01-02T03:60:05Z",
                              "2026-01-02T03:04:61Z",
                              "-026-01-02T03:04:05Z",
                              "+2026-01-02T03:04:05Z",
                              "02026-01-02T03:04:05Z",
                              "26-1-2T3:4:5Z",
                              "2026-+1-02T03:04:05Z",
                              "2026/01/02T03:04:05Z",
                              "2026-01-02 03:04:05Z",
                              "2026-01-02T03-04-05Z",
                              "2026-01-02T003:04:05Z",
                              "2026-01-02T03:04:05999Z",
                              "2026-01-02T03:04:09999999995",
                              "2026-01-02T03:04:05.Z",
                              "2026-01-02T03:04:05,5Z",
                              "2026-01-02T03:04:05.678ZZ",
                              "2026-01-02T03:04:05.678Z garbage",
                              "2026-01-02T03:04:05+02:00junk",
                              "2026-01-02T03:04:05+0200",
                              "2026-01-02T03:04:05+02",
                              "2026-01-02T03:04:05 +02:00",
                              "2026-01-02T03:04:05+24:00",
                              "2026-01-02T03:04:05+02:60",
                              "2026-01-02T03:04:05+99:99",
                              "2026-01-02T03:04:05+2:00",
                              "2026-01-02T03:04:0502:00",
                              "9999-99-99T99:99:99.999Z",
                              "２０２６-01-02T03:04:05Z",
                              "2026-01-02T03:04:05Z\n"}) {
        EXPECT_FALSE(parse_rfc3339(input).has_value()) << input;
    }
    EXPECT_FALSE(parse_rfc3339(std::string("2026-01-02T03:04:05Z\0x", 22)).has_value());
}

TEST(DateTest, lenient_parsing_accepts_deviations_from_rfc3339) {
    EXPECT_EQ(parse_rfc3339_lenient("2026-01-02T03:04:05"), utc(jan_2, 11045s));
    EXPECT_EQ(parse_rfc3339_lenient("2026-01-02T03:04:05.678"), utc(jan_2, 11045678ms));
    EXPECT_EQ(parse_rfc3339_lenient("2026-01-02T03:04:05,678Z"), utc(jan_2, 11045678ms));
    EXPECT_EQ(parse_rfc3339_lenient("2026-01-02T05:04:05+0200"), utc(jan_2, 11045s));
    EXPECT_EQ(parse_rfc3339_lenient("2026-01-02T05:04:05+02"), utc(jan_2, 11045s));
    EXPECT_EQ(parse_rfc3339_lenient("2026-01-01T23:34:05-0330"), utc(jan_2, 11045s));
}

TEST(DateTest, lenient_parsing_accepts_everything_rfc3339_allows) {
    for (const auto* input :
         {"2026-01-02T03:04:05Z", "2026-01-02t03:04:05.678z", "2026-01-01T23:34:05.5-03:30", "2016-12-31T23:59:60Z"}) {
        EXPECT_EQ(parse_rfc3339_lenient(input), parse_rfc3339(input)) << input;
    }
}

TEST(DateTest, lenient_parsing_rejects_invalid_date_times) {
    for (const auto* input :
         {"", "2026-01-02", "2026-01-02T03:04", "2026-1-2T3:4:5", "2026-01-02T03:04:05.", "2026-01-02T03:04:05,",
          "2026-01-02T03:04:05Z garbage", "2026-01-02T03:04:05 ", "2026-01-02T03:04:05+2", "2026-01-02T03:04:05+020",
          "2026-01-02T03:04:05+02:0", "2026-01-02T03:04:05+0260", "2026-01-02T03:04:05+2400", "2026-01-02T03:04:05+02x",
          "2026-01-02T03:04:050200", "2026-01-02T03:04:05-3"}) {
        EXPECT_FALSE(parse_rfc3339_lenient(input).has_value()) << input;
    }
}

TEST(DateTest, parses_up_to_the_first_and_last_nanosecond_of_utc_time_point) {
    const auto first = utc_time_point::min();
    const auto last = utc_time_point::max();
    EXPECT_EQ(parse_rfc3339(date::format("%FT%TZ", first)), first);
    EXPECT_EQ(parse_rfc3339(date::format("%FT%TZ", last)), last);
    EXPECT_FALSE(parse_rfc3339(limit_with_last_digit_moved(first, -1)).has_value());
    EXPECT_FALSE(parse_rfc3339(limit_with_last_digit_moved(last, +1)).has_value());
    EXPECT_FALSE(parse_rfc3339_lenient(limit_with_last_digit_moved(first, -1)).has_value());
    EXPECT_FALSE(parse_rfc3339_lenient(limit_with_last_digit_moved(last, +1)).has_value());
}

TEST(DateTest, applies_the_utc_offset_before_checking_the_range) {
    EXPECT_EQ(parse_rfc3339("1677-09-20T23:59:00-01:00"),
              utc(date::year_month_day{date::year{1677}, date::month{9}, date::day{21}}, 59min));
    EXPECT_EQ(parse_rfc3339("2262-04-12T00:00:00+01:00"),
              utc(date::year_month_day{date::year{2262}, date::month{4}, date::day{11}}, 23h));
    EXPECT_FALSE(parse_rfc3339("1677-09-21T00:00:00+00:01").has_value());
    EXPECT_FALSE(parse_rfc3339("2262-04-11T00:00:00-23:59").has_value());
}

TEST(DateTest, rejects_dates_outside_the_range_of_utc_time_point) {
    for (const auto* input : {"0000-01-01T00:00:00Z", "1600-01-01T00:00:00Z", "1677-09-21T00:00:00Z",
                              "2262-04-12T00:00:00Z", "2300-01-01T00:00:00Z", "9999-12-31T23:59:59Z"}) {
        EXPECT_FALSE(parse_rfc3339(input).has_value()) << input;
        EXPECT_FALSE(parse_rfc3339_lenient(input).has_value()) << input;
    }
    EXPECT_FALSE(parse_rfc3339_lenient("2300-01-01T00:00:00").has_value());
}

TEST(DateTest, clamps_dates_outside_the_range_of_utc_time_point) {
    for (const auto* input : {"2262-04-12T00:00:00Z", "2262-04-11T00:00:00-23:59", "2400-02-29T00:00:00",
                              "9999-12-31T23:59:59.999999999+0000"}) {
        EXPECT_EQ(parse_rfc3339_lenient_clamped(input), utc_time_point::max()) << input;
    }
    for (const auto* input : {"0000-01-01T00:00:00Z", "1677-09-21T00:00:00+00:01", "1600-02-29T00:00:00,5Z"}) {
        EXPECT_EQ(parse_rfc3339_lenient_clamped(input), utc_time_point::min()) << input;
    }
    for (const auto* input : {"9999-02-30T00:00:00Z", "9999-12-31T23:59:60Z", "9999-12-31T23:59:59Z garbage",
                              "9999-12-31T24:00:00Z", "99999-12-31T23:59:59Z"}) {
        EXPECT_FALSE(parse_rfc3339_lenient_clamped(input).has_value()) << input;
    }
    EXPECT_EQ(parse_rfc3339_lenient_clamped("2026-01-02T05:04:05+02"), utc(jan_2, 11045s));
}

TEST(DateTest, formats_rfc3339_in_utc_with_milliseconds) {
    EXPECT_EQ(to_rfc3339(utc(jan_2, 11045s)), "2026-01-02T03:04:05.000Z");
    EXPECT_EQ(to_rfc3339(utc(jan_2, 11045s + 678999999ns)), "2026-01-02T03:04:05.678Z");
    EXPECT_EQ(to_rfc3339(*parse_rfc3339("2016-12-31T23:59:60.25Z")), "2016-12-31T23:59:60.250Z");
}

TEST(DateTest, formats_independently_of_the_global_locale) {
    struct CommaDecimalPoint : std::numpunct<char> {
    protected:
        char do_decimal_point() const override {
            return ',';
        }
    };
    const auto previous = std::locale::global(std::locale(std::locale::classic(), new CommaDecimalPoint));
    const auto formatted = to_rfc3339(utc(jan_2, 11045678ms));
    std::locale::global(previous);
    EXPECT_EQ(formatted, "2026-01-02T03:04:05.678Z");
}

TEST(DateTest, round_trips_through_to_rfc3339) {
    for (const auto& time_point : {utc_time_point::min(), utc_time_point::max(), utc_time_point{},
                                   *parse_rfc3339("2016-12-31T23:59:60.5Z"), utc(jan_2, 11045678ms)}) {
        const auto formatted = to_rfc3339(time_point);
        const auto parsed = parse_rfc3339(formatted);
        ASSERT_TRUE(parsed.has_value()) << formatted;
        EXPECT_EQ(to_rfc3339(*parsed), formatted);
    }
}

} // namespace
