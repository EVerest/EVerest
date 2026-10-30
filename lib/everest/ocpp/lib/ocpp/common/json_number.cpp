// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <ocpp/common/json_number.hpp>

#include <cmath>
#include <limits>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace ocpp {

std::int32_t integer_from_json(const json& j) {
    if (j.is_number_unsigned()) {
        const auto value = j.get<std::uint64_t>();
        if (value <= static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
            return static_cast<std::int32_t>(value);
        }
    } else if (j.is_number_integer()) {
        const auto value = j.get<std::int64_t>();
        if (value >= std::numeric_limits<std::int32_t>::min() && value <= std::numeric_limits<std::int32_t>::max()) {
            return static_cast<std::int32_t>(value);
        }
    } else if (j.is_number_float()) {
        throw json::type_error::create(302, "number " + j.dump() + " is not an integer", &j);
    } else {
        throw json::type_error::create(302, "type must be integer, but is " + std::string(j.type_name()), &j);
    }
    throw json::out_of_range::create(406, "integer " + j.dump() + " does not fit into 32 bits", &j);
}

float decimal_from_json(const json& j) {
    if (!j.is_number()) {
        throw json::type_error::create(302, "type must be number, but is " + std::string(j.type_name()), &j);
    }
    const auto value = j.get<double>();
    if (!std::isfinite(value) || std::fabs(value) > std::numeric_limits<float>::max()) {
        std::ostringstream message;
        message << "decimal " << value << " does not fit into a float";
        throw json::out_of_range::create(406, message.str(), &j);
    }
    return static_cast<float>(value);
}

} // namespace ocpp
