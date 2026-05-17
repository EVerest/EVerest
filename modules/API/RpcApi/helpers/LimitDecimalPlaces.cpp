// SPDX-License-Identifier: Apache-2.0
// Copyright chargebyte GmbH and Contributors to EVerest

#include "LimitDecimalPlaces.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace helpers {

// This function formats and rounds a double value to a specified number of decimal places
double round_double(double value, double step, int precision) {
    double rounded = std::round(value / step) * step;

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << rounded;

    return std::stod(oss.str());
}

// This function recursively rounds all float values in a JSON object or array
void round_floats_in_json(nlohmann::json& j, int precision) {
    if (precision == 0) {
        return; // No rounding needed if precision is 0
    }
    if (j.is_object() || j.is_array()) {
        for (auto& el : j) {
            round_floats_in_json(el, precision);
        }
    } else if (j.is_number_float()) {
        const double step = std::pow(10.0, -precision);
        const double value = j.get<double>();
        j = round_double(value, step, precision);
    }
}
} // namespace helpers
