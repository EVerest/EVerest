// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "catalog.hpp"

namespace telemetry_router {

inline constexpr std::size_t OCPP_MAX_VALUE_LENGTH = 1000;

struct NumberFormat {
    int decimals{3};
    double scale{1.0};
};

/// \brief Format a numeric value in fixed notation with trailing zeros removed
/// \returns std::nullopt for non-finite values
std::optional<std::string> format_number(double value, const NumberFormat& format);

/// \brief Text of a sample value as written to an OCPP variable
/// \returns std::nullopt if the value does not fit the element (wrong type, non-finite number, unknown enum value)
std::optional<std::string> format_ocpp_value(const ElementDeclaration& element, const nlohmann::json& value,
                                             const NumberFormat& format);

} // namespace telemetry_router
