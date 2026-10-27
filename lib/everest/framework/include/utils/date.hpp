// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef UTILS_DATE_HPP
#define UTILS_DATE_HPP

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

std::string to_rfc3339(const std::chrono::time_point<date::utc_clock>& t);

/// \brief Convert a relative time in seconds from now, as reported by ISO 15118 and DIN SPEC 70121, into an
/// absolute RFC3339 UTC timestamp
std::string seconds_from_now_to_rfc3339(double seconds);

std::chrono::time_point<date::utc_clock> from_rfc3339_slow(const std::string& t);

std::chrono::time_point<date::utc_clock> from_rfc3339(const std::string& t);

} // namespace Date
} // namespace Everest

#endif // UTILS_CONFIG_HPP
