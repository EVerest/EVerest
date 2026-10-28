// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"

#include <everest/util/json/nesting_depth.hpp>

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

// A scalar whose schema is "number" or "integer" has to be a JSON number: nlohmann::json would
// otherwise turn a boolean into 0 or 1 and truncate a fraction for an integer.
template <class T> T parse_json_number(std::string_view val) {
    const nlohmann::json json = parse_json<nlohmann::json>(val);
    const bool accepted = std::is_floating_point_v<T> ? json.is_number()
                          : std::is_unsigned_v<T>     ? json.is_number_unsigned()
                                                      : json.is_number_integer();
    if (not accepted) {
        throw std::invalid_argument("JSON value is not a number of the expected kind");
    }
    return json.get<T>();
}

} // namespace everest::lib::API::V1_0::types::utilities
