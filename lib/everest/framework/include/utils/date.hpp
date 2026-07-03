// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
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

std::chrono::time_point<date::utc_clock> from_rfc3339(const std::string& t);

} // namespace Date
} // namespace Everest

#endif // UTILS_CONFIG_HPP
