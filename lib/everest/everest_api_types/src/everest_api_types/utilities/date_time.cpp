// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "utilities/date_time.hpp"

#include <stdexcept>

#include <everest/date/date.hpp>

namespace everest::lib::API {

std::string date_time_from_json(const nlohmann::json& j) {
    const auto value = j.get<std::string>();
    const auto time_point = everest::lib::datetime::parse_rfc3339(value);
    if (not time_point.has_value()) {
        throw std::invalid_argument("not a date-time: " + value);
    }
    return everest::lib::datetime::to_rfc3339(*time_point);
}

} // namespace everest::lib::API
