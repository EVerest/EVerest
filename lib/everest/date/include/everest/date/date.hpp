// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include <date/tz.h>

namespace everest::lib::datetime {

using utc_time_point = std::chrono::time_point<date::utc_clock>;

/// \brief Parse an RFC 3339 date-time: YYYY-MM-DDTHH:MM:SS[.frac](Z|+HH:MM|-HH:MM)
///
/// 'T' and 'Z' may also be lowercase. Fractional seconds are kept to nanoseconds, further digits are ignored. Second
/// 60 is accepted only for an actual leap second.
/// \returns the parsed time point, or std::nullopt if \p input is not a valid RFC 3339 date-time or lies outside the
/// range of utc_time_point (1677-09-21T00:12:43.145224192Z to 2262-04-11T23:46:49.854775807Z)
std::optional<utc_time_point> parse_rfc3339(std::string_view input);

/// \brief Parse an RFC 3339 date-time, tolerating deviations that ISO 8601 extended format allows
///
/// Accepts everything parse_rfc3339() accepts. Additionally the fraction may start with a comma, and the UTC offset may
/// be written as +HHMM or +HH or be left out, which is read as UTC where ISO 8601 would mean local time. Other ISO 8601
/// representations, such as the basic format, ordinal and week dates or reduced precision, are rejected.
/// \returns the parsed time point, or std::nullopt if \p input is not a valid date-time or lies outside the range of
/// utc_time_point
std::optional<utc_time_point> parse_rfc3339_lenient(std::string_view input);

/// \brief Parse like parse_rfc3339_lenient(), but clamp date-times outside the range of utc_time_point to
/// utc_time_point::min() or utc_time_point::max() instead of rejecting them
///
/// For peers that write far-off dates such as 9999-12-31T23:59:59Z to mean "never".
/// \returns the parsed or clamped time point, or std::nullopt if \p input is not a valid date-time
std::optional<utc_time_point> parse_rfc3339_lenient_clamped(std::string_view input);

/// \brief Format \p time_point as an RFC 3339 date-time in UTC: YYYY-MM-DDTHH:MM:SS.mmmZ
///
/// The time point is truncated towards zero to milliseconds, the precision OCPP 2.x allows. A leap second is formatted
/// as second 60. Every result parses back with parse_rfc3339().
std::string to_rfc3339(const utc_time_point& time_point);

} // namespace everest::lib::datetime
