// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"

#include <everest/util/json/nesting_depth.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace everest::lib::API::V1_0::types::utilities {

template <class T> std::string dump_json(T const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

template <class T> T parse_json(std::string_view val) {
    if (everest::lib::util::exceeds_json_nesting_depth(val, max_json_nesting_depth)) {
        throw std::invalid_argument("JSON nesting exceeds the maximum depth");
    }
    return nlohmann::json::parse(val.begin(), val.end());
}

// A value whose schema is "number" or "integer" has to be a JSON number that T can hold: nlohmann::json
// would otherwise turn a boolean into 0 or 1, truncate a fraction for an integer, wrap an integer that
// does not fit T (2^32 + 1 to 1 for int32_t) and turn a float beyond FLT_MAX into infinity.
template <class T> T json_number(nlohmann::json const& json) {
    const bool accepted = std::is_floating_point_v<T> ? json.is_number()
                          : std::is_unsigned_v<T>     ? json.is_number_unsigned()
                                                      : json.is_number_integer();
    if (not accepted) {
        throw std::invalid_argument("JSON value is not a number of the expected kind");
    }
    bool in_range = true;
    if constexpr (std::is_floating_point_v<T>) {
        const auto value = json.get<double>();
        in_range = std::isfinite(value) and std::abs(value) <= static_cast<double>(std::numeric_limits<T>::max());
    } else if (json.is_number_unsigned()) {
        // also a positive value of a signed T: nlohmann stores every non-negative integer as unsigned
        in_range = json.get<std::uint64_t>() <= static_cast<std::uint64_t>(std::numeric_limits<T>::max());
    } else if constexpr (std::is_signed_v<T>) {
        const auto value = json.get<std::int64_t>();
        in_range = value >= std::numeric_limits<T>::min() and value <= std::numeric_limits<T>::max();
    }
    if (not in_range) {
        throw std::invalid_argument("JSON number is out of range");
    }
    return json.get<T>();
}

template <class T> T parse_json_number(std::string_view val) {
    return json_number<T>(parse_json<nlohmann::json>(val));
}

} // namespace everest::lib::API::V1_0::types::utilities
