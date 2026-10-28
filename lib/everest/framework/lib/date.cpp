// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <cstdint>
#include <exception>
#include <string_view>

#include <everest/logging.hpp>
#include <utils/date.hpp>

namespace Everest {
namespace Date {

void preload_tzdb() {
    try {
        date::get_tzdb();
    } catch (const std::exception& e) {
        EVLOG_warning << "tzdb preload failed; timestamps may be unreliable: " << e.what();
    }
}

std::string to_rfc3339(const std::chrono::time_point<date::utc_clock>& t) {
    return date::format("%FT%TZ", std::chrono::time_point_cast<std::chrono::milliseconds>(t));
}

std::string seconds_from_now_to_rfc3339(double seconds) {
    const auto offset = std::chrono::duration_cast<date::utc_clock::duration>(std::chrono::duration<double>(seconds));
    return to_rfc3339(date::utc_clock::now() + offset);
}

std::chrono::time_point<date::utc_clock> from_rfc3339_slow(const std::string& t) {
    std::istringstream infile{t};
    std::chrono::time_point<date::utc_clock> tp;
    infile >> date::parse("%FT%T", tp);
    return tp;
}

namespace {

bool is_digit(char c) {
    return c >= '0' and c <= '9';
}

bool has_char_at(std::string_view input, std::size_t pos, char c) {
    return pos < input.size() and input[pos] == c;
}

std::optional<int> parse_fixed_digits(std::string_view input, std::size_t pos, std::size_t count) {
    if (pos + count > input.size()) {
        return std::nullopt;
    }
    int value = 0;
    for (std::size_t i = pos; i < pos + count; ++i) {
        if (not is_digit(input[i])) {
            return std::nullopt;
        }
        value = value * 10 + (input[i] - '0');
    }
    return value;
}

} // namespace

std::optional<std::chrono::time_point<date::utc_clock>> try_from_rfc3339(const std::string& t) {
    using namespace std::chrono;
    const std::string_view input(t);

    const auto y = parse_fixed_digits(input, 0, 4);
    const auto m = parse_fixed_digits(input, 5, 2);
    const auto d = parse_fixed_digits(input, 8, 2);
    const auto h = parse_fixed_digits(input, 11, 2);
    const auto M = parse_fixed_digits(input, 14, 2);
    const auto s = parse_fixed_digits(input, 17, 2);
    if (not(y and m and d and h and M and s) or not has_char_at(input, 4, '-') or not has_char_at(input, 7, '-') or
        not(has_char_at(input, 10, 'T') or has_char_at(input, 10, 't')) or not has_char_at(input, 13, ':') or
        not has_char_at(input, 16, ':')) {
        return std::nullopt;
    }

    const date::year_month_day ymd{date::year{*y}, date::month{static_cast<unsigned>(*m)},
                                   date::day{static_cast<unsigned>(*d)}};
    // RFC 3339 5.7: second 60 is a leap second
    if (not ymd.ok() or *h > 23 or *M > 59 or *s > 60) {
        return std::nullopt;
    }

    std::size_t pos = 19;
    nanoseconds fraction{0};
    if (has_char_at(input, pos, '.')) {
        const auto first_digit = ++pos;
        std::int64_t scale = 100'000'000;
        for (; pos < input.size() and is_digit(input[pos]); ++pos) {
            fraction += nanoseconds{(input[pos] - '0') * scale};
            scale /= 10;
        }
        if (pos == first_digit) {
            return std::nullopt;
        }
    }

    minutes offset{0};
    if (has_char_at(input, pos, 'Z') or has_char_at(input, pos, 'z')) {
        ++pos;
    } else if (has_char_at(input, pos, '+') or has_char_at(input, pos, '-')) {
        const auto offset_h = parse_fixed_digits(input, pos + 1, 2);
        const auto offset_m = parse_fixed_digits(input, pos + 4, 2);
        if (not offset_h or not offset_m or not has_char_at(input, pos + 3, ':') or *offset_h > 23 or *offset_m > 59) {
            return std::nullopt;
        }
        offset = hours{*offset_h} + minutes{*offset_m};
        if (input[pos] == '-') {
            offset = -offset;
        }
        pos += 6;
    } else {
        return std::nullopt;
    }

    if (pos != input.size()) {
        return std::nullopt;
    }

    const seconds leap{*s == 60 ? 1 : 0};
    const auto sys_time = date::sys_days{ymd} + hours{*h} + minutes{*M} + seconds{*s} - leap + fraction - offset;
    return time_point_cast<date::utc_clock::duration>(date::utc_clock::from_sys(sys_time) + leap);
}

std::chrono::time_point<date::utc_clock> from_rfc3339(const std::string& t) {
    if (const auto tp = try_from_rfc3339(t)) {
        return *tp;
    }
    return from_rfc3339_slow(t);
}

} // namespace Date
} // namespace Everest
