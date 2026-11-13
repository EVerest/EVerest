// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <exception>
#include <limits>
#include <string_view>

#include <everest/date/date.hpp>
#include <everest/logging.hpp>
#include <utils/date.hpp>

namespace Everest {
namespace Date {

namespace {

bool is_digit(char c) {
    return c >= '0' and c <= '9';
}

// Length of YYYY-MM-DDTHH:MM:SS[.frac], or 0 if a digit follows it
std::size_t date_time_length(std::string_view input) {
    std::size_t end = 19;
    if (end + 1 < input.size() and input[end] == '.' and is_digit(input[end + 1])) {
        for (end += 2; end < input.size() and is_digit(input[end]); ++end) {
        }
    }
    return end < input.size() and is_digit(input[end]) ? 0 : end;
}

std::optional<std::chrono::time_point<date::utc_clock>> parse_ignoring_utc_offset(std::string_view input) {
    return everest::lib::datetime::parse_rfc3339_lenient(input.substr(0, date_time_length(input)));
}

} // namespace

void preload_tzdb() {
    try {
        date::get_tzdb();
    } catch (const std::exception& e) {
        EVLOG_warning << "tzdb preload failed; timestamps may be unreliable: " << e.what();
    }
}

std::string to_rfc3339(const std::chrono::time_point<date::utc_clock>& t) {
    return everest::lib::datetime::to_rfc3339(t);
}

std::string seconds_from_now_to_rfc3339(std::uint32_t seconds) {
    return to_rfc3339(date::utc_clock::now() + std::chrono::seconds(seconds));
}

std::uint32_t clamp_to_seconds(double seconds) {
    // Written as not(> 0) so that NaN is rejected too
    if (not(seconds > 0.0)) {
        return 0;
    }
    constexpr auto max_seconds = static_cast<double>(std::numeric_limits<std::uint32_t>::max());
    return static_cast<std::uint32_t>(std::round(std::min(seconds, max_seconds)));
}

std::int32_t rfc3339_to_seconds_from_now(const std::string& t) {
    const auto time_until = from_rfc3339(t) - date::utc_clock::now();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(time_until).count();
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(seconds, std::numeric_limits<std::int32_t>::min(),
                                                              std::numeric_limits<std::int32_t>::max()));
}

std::optional<std::chrono::time_point<date::utc_clock>> try_from_rfc3339(const std::string& t) {
    return everest::lib::datetime::parse_rfc3339(t);
}

std::chrono::time_point<date::utc_clock> from_rfc3339(const std::string& t) {
    if (const auto tp = try_from_rfc3339(t)) {
        return *tp;
    }
    return parse_ignoring_utc_offset(t).value_or(date::utc_clock::time_point{});
}

} // namespace Date
} // namespace Everest
