// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef UTILS_DATE_HPP
#define UTILS_DATE_HPP

#include <optional>
#include <string>

#include <date/date.h>
#include <date/tz.h>

namespace Everest {
namespace Date {

/// \brief Force single-threaded initialization of the HowardHinnant date tzdb /
/// leap-second singleton.
///
/// Call once at process start, before any worker thread. The tzdb is initialized
/// lazily on first use; letting that first use happen concurrently from multiple
/// threads races the initialization and can crash inside the leap-second lookup.
/// Warming it up single-threaded closes that window.
void preload_tzdb();

/// \brief Format \p t as an RFC 3339 date-time in UTC (YYYY-MM-DDTHH:MM:SS.mmmZ).
///
/// The time point is truncated to milliseconds. A leap second is formatted as second 60.
std::string to_rfc3339(const std::chrono::time_point<date::utc_clock>& t);

/// \brief Strict parsing of RFC 3339 date-time (YYYY-MM-DDTHH:MM:SS[.frac](Z|+HH:MM|-HH:MM)), see
/// everest::lib::datetime::parse_rfc3339().
///
/// Field ranges, separators and the time offset are validated. Fractional seconds are kept to nanoseconds.
/// \returns the parsed time point, or std::nullopt if \p t is not a valid RFC 3339 date-time or lies outside the
/// range of utc_clock (1677-09-21T00:12:43.145224192Z to 2262-04-11T23:46:49.854775807Z)
std::optional<std::chrono::time_point<date::utc_clock>> try_from_rfc3339(const std::string& t);

/// \brief Parse \p t with try_from_rfc3339(), falling back to lenient parsing of YYYY-MM-DDTHH:MM:SS[.frac] for
/// inputs it rejects.
///
/// The lenient fallback validates field ranges, but ignores the time offset and anything following the seconds, and
/// always interprets the result as UTC. Never throws. Input that neither accepts yields the default-constructed
/// (epoch) time point; use try_from_rfc3339() to detect invalid input.
std::chrono::time_point<date::utc_clock> from_rfc3339(const std::string& t);

} // namespace Date
} // namespace Everest

#endif // UTILS_CONFIG_HPP
