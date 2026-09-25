// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#include "value_format.hpp"

#include <algorithm>
#include <cmath>

#include <fmt/format.h>

namespace telemetry_router {

std::optional<std::string> format_number(double value, const NumberFormat& format) {
    const auto scaled = value * format.scale;
    if (not std::isfinite(scaled)) {
        return std::nullopt;
    }
    auto text = fmt::format("{:.{}f}", scaled, std::max(format.decimals, 0));
    if (text.find('.') != std::string::npos) {
        text.erase(text.find_last_not_of('0') + 1);
        if (text.back() == '.') {
            text.pop_back();
        }
    }
    if (text == "-0") {
        text = "0";
    }
    return text;
}

std::optional<std::string> format_ocpp_value(const ElementDeclaration& element, const nlohmann::json& value,
                                             const NumberFormat& format) {
    switch (element.value_type) {
    case ValueType::Integer:
        if (not value.is_number_integer()) {
            return std::nullopt;
        }
        if (format.scale == 1.0) {
            return std::to_string(value.get<std::int64_t>());
        }
        return format_number(value.get<double>(), format);
    case ValueType::Number:
        if (not value.is_number()) {
            return std::nullopt;
        }
        return format_number(value.get<double>(), format);
    case ValueType::Boolean:
        if (not value.is_boolean()) {
            return std::nullopt;
        }
        return value.get<bool>() ? std::string("true") : std::string("false");
    case ValueType::String: {
        if (not value.is_string()) {
            return std::nullopt;
        }
        auto text = value.get<std::string>();
        if (not element.enum_values.empty() and
            std::find(element.enum_values.begin(), element.enum_values.end(), text) == element.enum_values.end()) {
            return std::nullopt;
        }
        if (text.size() > OCPP_MAX_VALUE_LENGTH) {
            text.resize(OCPP_MAX_VALUE_LENGTH);
        }
        return text;
    }
    case ValueType::Object:
        return std::nullopt;
    }
    return std::nullopt;
}

} // namespace telemetry_router
