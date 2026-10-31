// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <cstddef>
#include <exception>
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
