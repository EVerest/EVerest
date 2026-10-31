// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/date/date.hpp>

#include <cstddef>
#include <cstdint>
#include <locale>
#include <sstream>

namespace everest::lib::datetime {

namespace {

enum class Profile {
    rfc3339,
    lenient,
};

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

// utc_time_point::duration counts nanoseconds, which covers only the years 1678 to 2262
bool fits_utc_time_point(std::chrono::seconds since_epoch, utc_time_point::duration subseconds) {
    using std::chrono::seconds;
    using duration = utc_time_point::duration;
    const auto first = std::chrono::ceil<seconds>(duration::min());
    const auto last = std::chrono::floor<seconds>(duration::max());
    if (since_epoch == first - seconds{1}) {
        return subseconds >= duration::min() + seconds{1} - std::chrono::duration_cast<duration>(first);
    }
    if (since_epoch == last) {
        return subseconds <= duration::max() - std::chrono::duration_cast<duration>(last);
    }
    return since_epoch >= first and since_epoch < last;
}

std::optional<std::chrono::nanoseconds> parse_fraction(std::string_view input, std::size_t& pos, Profile profile) {
    std::chrono::nanoseconds fraction{0};
    if (not has_char_at(input, pos, '.') and not(profile == Profile::lenient and has_char_at(input, pos, ','))) {
        return fraction;
    }
    const auto first_digit = ++pos;
    std::int64_t scale = 100'000'000;
    for (; pos < input.size() and is_digit(input[pos]); ++pos) {
        fraction += std::chrono::nanoseconds{(input[pos] - '0') * scale};
        scale /= 10;
    }
    if (pos == first_digit) {
        return std::nullopt;
    }
    return fraction;
}

std::optional<std::chrono::minutes> parse_utc_offset(std::string_view input, std::size_t& pos, Profile profile) {
    if (has_char_at(input, pos, 'Z') or has_char_at(input, pos, 'z')) {
        ++pos;
        return std::chrono::minutes{0};
    }
    if (not has_char_at(input, pos, '+') and not has_char_at(input, pos, '-')) {
        if (profile == Profile::lenient) {
            return std::chrono::minutes{0};
        }
        return std::nullopt;
    }

    const bool negative = input[pos] == '-';
    const auto hours = parse_fixed_digits(input, pos + 1, 2);
    std::optional<int> minutes;
    auto end = pos + 3;
    if (has_char_at(input, end, ':')) {
        minutes = parse_fixed_digits(input, end + 1, 2);
        end += 3;
    } else if (profile == Profile::lenient and end == input.size()) {
        minutes = 0;
    } else if (profile == Profile::lenient) {
        minutes = parse_fixed_digits(input, end, 2);
        end += 2;
    }
    if (not hours or not minutes or *hours > 23 or *minutes > 59) {
        return std::nullopt;
    }
    pos = end;
    const auto offset = std::chrono::hours{*hours} + std::chrono::minutes{*minutes};
    return negative ? -offset : offset;
}

enum class OutOfRange {
    reject,
    clamp,
};

std::optional<utc_time_point> parse(std::string_view input, Profile profile, OutOfRange out_of_range) {
    using namespace std::chrono;

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
    if (not ymd.ok() or *h > 23 or *M > 59 or *s > 60) {
        return std::nullopt;
    }

    std::size_t pos = 19;
    const auto fraction = parse_fraction(input, pos, profile);
    if (not fraction) {
        return std::nullopt;
    }
    const auto offset = parse_utc_offset(input, pos, profile);
    if (not offset or pos != input.size()) {
        return std::nullopt;
    }

    // RFC 3339 5.7: second 60 is a leap second, which utc_clock counts right after second 59
    const seconds leap{*s == 60 ? 1 : 0};
    const date::sys_seconds sys_time = date::sys_days{ymd} + hours{*h} + minutes{*M} + seconds{*s} - leap - *offset;
    const auto utc_time = date::utc_clock::from_sys(sys_time) + leap;
    if (leap.count() != 0 and not date::get_leap_second_info(utc_time).is_leap_second) {
        return std::nullopt;
    }
    if (not fits_utc_time_point(utc_time.time_since_epoch(), *fraction)) {
        if (out_of_range == OutOfRange::reject) {
            return std::nullopt;
        }
        return utc_time.time_since_epoch().count() < 0 ? utc_time_point::min() : utc_time_point::max();
    }
    return time_point_cast<utc_time_point::duration>(utc_time) + *fraction;
}

} // namespace

std::optional<utc_time_point> parse_rfc3339(std::string_view input) {
    return parse(input, Profile::rfc3339, OutOfRange::reject);
}

std::optional<utc_time_point> parse_rfc3339_lenient(std::string_view input) {
    return parse(input, Profile::lenient, OutOfRange::reject);
}

std::optional<utc_time_point> parse_rfc3339_lenient_clamped(std::string_view input) {
    return parse(input, Profile::lenient, OutOfRange::clamp);
}

std::string to_rfc3339(const utc_time_point& time_point) {
    // do not use date::format here because that takes the decimal separator from the global locale
    std::ostringstream out;
    out.imbue(std::locale::classic());
    date::to_stream(out, "%FT%TZ", std::chrono::time_point_cast<std::chrono::milliseconds>(time_point));
    return out.str();
}

} // namespace everest::lib::datetime
