// SPDX-License-Identifier: Apache-2.0
// Copyright chargebyte GmbH and Contributors to EVerest
#ifndef LIMIT_DECIMAL_PLACES_HPP
#define LIMIT_DECIMAL_PLACES_HPP

#include <nlohmann/json.hpp>

namespace helpers {
double round_double(double value, int precision = 3);
void round_floats_in_json(nlohmann::json& j, int precision = 3);
} // namespace helpers

#endif // LIMIT_DECIMAL_PLACES_HPP
