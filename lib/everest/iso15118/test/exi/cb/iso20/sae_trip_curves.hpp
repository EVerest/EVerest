// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstddef>

#include <catch2/catch_test_macros.hpp>

#include <iso15118/message/ac_der_sae_types.hpp>
#include <iso15118/message/common_types.hpp>

using namespace iso15118;

// AMD1 M.2.2.1.10 and M.2.2.1.11: x is the duration in s, y the voltage or frequency.
inline void require_trip_curve(const message_20::datatypes::sae::DERCurve& curve,
                               message_20::datatypes::sae::DERUnit expected_y_unit) {
    using message_20::datatypes::from_RationalNumber;
    REQUIRE(curve.x_unit == message_20::datatypes::sae::DERUnit::s);
    REQUIRE(curve.y_unit == expected_y_unit);
    const auto& points = curve.curve_data_points;
    REQUIRE(points.size() >= message_20::datatypes::sae::CurveDataPointsMinLength);
    for (std::size_t i = 1; i < points.size(); ++i) {
        REQUIRE(from_RationalNumber(points[i - 1].x_value) <= from_RationalNumber(points[i].x_value));
    }
}

// The must-trip curves shared by the CPD and ChargeLoop response fixtures.
inline message_20::datatypes::sae::VoltageTrip make_voltage_trip() {
    using namespace message_20::datatypes::sae;
    VoltageTrip trip;
    auto& ov_must = trip.over_voltage_must_trip_curve;
    ov_must.enable = true;
    ov_must.x_unit = DERUnit::s;
    ov_must.y_unit = DERUnit::V;
    ov_must.curve_data_points.push_back(DataTuple{{2, -1}, {288, 0}});
    ov_must.curve_data_points.push_back(DataTuple{{1, 0}, {264, 0}});
    auto& uv_must = trip.under_voltage_must_trip_curve;
    uv_must.enable = true;
    uv_must.x_unit = DERUnit::s;
    uv_must.y_unit = DERUnit::V;
    uv_must.curve_data_points.push_back(DataTuple{{2, -1}, {160, 0}});
    uv_must.curve_data_points.push_back(DataTuple{{2, 0}, {196, 0}});
    return trip;
}

inline message_20::datatypes::sae::FrequencyTrip make_frequency_trip() {
    using namespace message_20::datatypes::sae;
    FrequencyTrip trip;
    auto& of_must = trip.over_frequency_must_trip_curve;
    of_must.enable = true;
    of_must.x_unit = DERUnit::s;
    of_must.y_unit = DERUnit::Hz;
    of_must.curve_data_points.push_back(DataTuple{{5, -1}, {63, 0}});
    of_must.curve_data_points.push_back(DataTuple{{1, 0}, {62, 0}});
    auto& uf_must = trip.under_frequency_must_trip_curve;
    uf_must.enable = true;
    uf_must.x_unit = DERUnit::s;
    uf_must.y_unit = DERUnit::Hz;
    uf_must.curve_data_points.push_back(DataTuple{{5, -1}, {57, 0}});
    uf_must.curve_data_points.push_back(DataTuple{{1, 0}, {58, 0}});
    return trip;
}
