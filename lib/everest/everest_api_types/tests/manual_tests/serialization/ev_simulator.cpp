// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "SerializationTestHelpers.hpp"
#include "everest_api_types/ev_simulator/API.hpp"
#include "everest_api_types/ev_simulator/codec.hpp"
#include "nlohmann/json.hpp"
#include <gtest/gtest.h>
#include <stdexcept>

// ChargingCurve and StartSessionParams are opted out of round-trip autogen via disable.csv:
// the generator emits a points vector with identical t_offset_ms across entries which violates
// the cross-element strict-monotonic invariant enforced in from_json(ChargingCurve&).
// These manual cases cover the happy paths plus the two rejection paths in from_json.

using namespace everest::lib::API::V1_0::types::ev_simulator;

namespace {

ChargingCurve make_valid_curve(bool loop) {
    ChargingCurve curve;
    curve.loop = loop;

    CurvePoint p0;
    p0.t_offset_ms = 0;
    p0.current_a = 6.0f;
    p0.three_phases = false;
    p0.ramp_ms.reset();
    curve.points.push_back(p0);

    CurvePoint p1;
    p1.t_offset_ms = 1000;
    p1.current_a = 16.0f;
    p1.three_phases = true;
    p1.ramp_ms.emplace(500);
    curve.points.push_back(p1);

    CurvePoint p2;
    p2.t_offset_ms = 5000;
    p2.current_a = 32.0f;
    p2.three_phases = true;
    p2.ramp_ms.emplace(2000);
    curve.points.push_back(p2);

    return curve;
}

void expect_curve_eq(ChargingCurve const& a, ChargingCurve const& b) {
    EXPECT_EQ(a.loop, b.loop);
    ASSERT_EQ(a.points.size(), b.points.size());
    for (size_t i = 0; i < a.points.size(); ++i) {
        SCOPED_TRACE("point index " + std::to_string(i));
        EXPECT_EQ(a.points[i].t_offset_ms, b.points[i].t_offset_ms);
        EXPECT_FLOAT_EQ(a.points[i].current_a, b.points[i].current_a);
        EXPECT_EQ(a.points[i].three_phases, b.points[i].three_phases);
        expect_opt_eq(a.points[i].ramp_ms, b.points[i].ramp_ms);
    }
}

} // namespace

TEST(ev_simulator, ChargingCurve_roundtrip_loop_true) {
    auto original = make_valid_curve(true);
    auto ser = serialize(original);
    auto des = deserialize<ChargingCurve>(ser);
    expect_curve_eq(original, des);
}

TEST(ev_simulator, ChargingCurve_roundtrip_loop_false) {
    auto original = make_valid_curve(false);
    auto ser = serialize(original);
    auto des = deserialize<ChargingCurve>(ser);
    expect_curve_eq(original, des);
}

TEST(ev_simulator, ChargingCurve_empty_points_rejected) {
    auto bad = R"({"points":[],"loop":false})";
    EXPECT_THROW(deserialize<ChargingCurve>(bad), std::out_of_range);
}

TEST(ev_simulator, ChargingCurve_non_monotonic_t_offset_rejected) {
    // two consecutive points with equal t_offset_ms violates the strict-monotonic invariant
    auto equal_offsets = R"({
        "points":[
            {"t_offset_ms":0,"current_a":6.0,"three_phases":false},
            {"t_offset_ms":1000,"current_a":16.0,"three_phases":true},
            {"t_offset_ms":1000,"current_a":24.0,"three_phases":true}
        ],
        "loop":false
    })";
    EXPECT_THROW(deserialize<ChargingCurve>(equal_offsets), std::out_of_range);

    // and the strictly-decreasing variant
    auto decreasing_offsets = R"({
        "points":[
            {"t_offset_ms":0,"current_a":6.0,"three_phases":false},
            {"t_offset_ms":2000,"current_a":16.0,"three_phases":true},
            {"t_offset_ms":1500,"current_a":24.0,"three_phases":true}
        ],
        "loop":false
    })";
    EXPECT_THROW(deserialize<ChargingCurve>(decreasing_offsets), std::out_of_range);
}

TEST(ev_simulator, StartSessionParams_roundtrip_with_curve_and_optionals) {
    StartSessionParams original;
    original.mode = ChargeMode::AcIso2;
    original.payment.emplace(PaymentOption::Contract);
    original.departure_time_s.emplace(3600);
    original.e_amount_wh.emplace(22000);
    original.charging_current_a.emplace(16.0f);
    original.three_phases.emplace(true);
    original.curve.emplace(make_valid_curve(true));

    auto ser = serialize(original);
    auto des = deserialize<StartSessionParams>(ser);

    EXPECT_EQ(original.mode, des.mode);
    expect_opt_eq(original.payment, des.payment);
    expect_opt_eq(original.departure_time_s, des.departure_time_s);
    expect_opt_eq(original.e_amount_wh, des.e_amount_wh);
    EXPECT_TRUE(des.charging_current_a.has_value());
    EXPECT_FLOAT_EQ(original.charging_current_a.value(), des.charging_current_a.value());
    expect_opt_eq(original.three_phases, des.three_phases);
    EXPECT_FALSE(des.bpt.has_value());
    EXPECT_FALSE(des.mcs.has_value());
    ASSERT_TRUE(des.curve.has_value());
    expect_curve_eq(original.curve.value(), des.curve.value());
}

TEST(ev_simulator, StartSessionParams_roundtrip_mode_only) {
    StartSessionParams original;
    original.mode = ChargeMode::AcIec;

    auto ser = serialize(original);
    auto des = deserialize<StartSessionParams>(ser);

    EXPECT_EQ(original.mode, des.mode);
    EXPECT_FALSE(des.payment.has_value());
    EXPECT_FALSE(des.departure_time_s.has_value());
    EXPECT_FALSE(des.e_amount_wh.has_value());
    EXPECT_FALSE(des.charging_current_a.has_value());
    EXPECT_FALSE(des.three_phases.has_value());
    EXPECT_FALSE(des.bpt.has_value());
    EXPECT_FALSE(des.mcs.has_value());
    EXPECT_FALSE(des.curve.has_value());
}

TEST(ev_simulator, StartSessionParams_roundtrip_bpt_and_mcs_no_curve) {
    StartSessionParams original;
    original.mode = ChargeMode::DcIso2;

    BptParams bpt;
    bpt.discharge_max_current_limit = 60.0f;
    bpt.discharge_max_power_limit = 11000.0f;
    bpt.discharge_target_current = 32.0f;
    bpt.discharge_minimal_soc = 20.0f;
    original.bpt.emplace(bpt);
    original.mcs.emplace(McsProfile{});

    auto ser = serialize(original);
    auto des = deserialize<StartSessionParams>(ser);

    EXPECT_EQ(original.mode, des.mode);
    ASSERT_TRUE(des.bpt.has_value());
    EXPECT_FLOAT_EQ(original.bpt->discharge_max_current_limit, des.bpt->discharge_max_current_limit);
    EXPECT_FLOAT_EQ(original.bpt->discharge_max_power_limit, des.bpt->discharge_max_power_limit);
    EXPECT_FLOAT_EQ(original.bpt->discharge_target_current, des.bpt->discharge_target_current);
    EXPECT_FLOAT_EQ(original.bpt->discharge_minimal_soc, des.bpt->discharge_minimal_soc);
    EXPECT_TRUE(des.mcs.has_value());
    EXPECT_FALSE(des.curve.has_value());
}
