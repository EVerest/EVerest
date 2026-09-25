// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_all.hpp>

#include <limits>

#include "value_format.hpp"

using namespace telemetry_router;
using nlohmann::json;

namespace {
ElementDeclaration element_of(ValueType value_type, std::vector<std::string> enum_values = {}) {
    ElementDeclaration element;
    element.name = "e";
    element.kind = value_type == ValueType::Boolean or value_type == ValueType::String ? Kind::State : Kind::Gauge;
    element.value_type = value_type;
    element.enum_values = std::move(enum_values);
    return element;
}
} // namespace

TEST_CASE("Numbers are written in fixed notation", "[telemetry_router]") {
    CHECK(format_number(41.2, {}) == std::optional<std::string>("41.2"));
    CHECK(format_number(16.0, {}) == std::optional<std::string>("16"));
    CHECK(format_number(1e-7, {}) == std::optional<std::string>("0"));
    CHECK(format_number(-1e-7, {}) == std::optional<std::string>("0"));
    CHECK(format_number(1e20, {}) == std::optional<std::string>("100000000000000000000"));
    CHECK(format_number(2.345678, {2, 1.0}) == std::optional<std::string>("2.35"));
    CHECK(format_number(12345.0, {0, 0.001}) == std::optional<std::string>("12"));
    CHECK_FALSE(format_number(std::numeric_limits<double>::infinity(), {}).has_value());
    CHECK_FALSE(format_number(std::numeric_limits<double>::quiet_NaN(), {}).has_value());
}

TEST_CASE("Values are formatted according to the element type", "[telemetry_router]") {
    CHECK(format_ocpp_value(element_of(ValueType::Integer), 4294967296, {}) ==
          std::optional<std::string>("4294967296"));
    CHECK(format_ocpp_value(element_of(ValueType::Integer), 1500, {0, 0.001}) == std::optional<std::string>("2"));
    CHECK_FALSE(format_ocpp_value(element_of(ValueType::Integer), 1.5, {}).has_value());
    CHECK(format_ocpp_value(element_of(ValueType::Number), 3, {}) == std::optional<std::string>("3"));
    CHECK(format_ocpp_value(element_of(ValueType::Boolean), true, {}) == std::optional<std::string>("true"));
    CHECK(format_ocpp_value(element_of(ValueType::Boolean), false, {}) == std::optional<std::string>("false"));
    CHECK_FALSE(format_ocpp_value(element_of(ValueType::Boolean), 1, {}).has_value());
    CHECK(format_ocpp_value(element_of(ValueType::String, {"Idle", "Charging"}), "Charging", {}) ==
          std::optional<std::string>("Charging"));
    CHECK_FALSE(format_ocpp_value(element_of(ValueType::String, {"Idle", "Charging"}), "Other", {}).has_value());
    CHECK(format_ocpp_value(element_of(ValueType::String), std::string(1200, 'x'), {})->size() ==
          OCPP_MAX_VALUE_LENGTH);
    CHECK_FALSE(format_ocpp_value(element_of(ValueType::Object), json::object(), {}).has_value());
}
