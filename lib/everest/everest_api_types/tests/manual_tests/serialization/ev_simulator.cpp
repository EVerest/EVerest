// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "SerializationTestHelpers.hpp"
#include "everest_api_types/ev_simulator/API.hpp"
#include "everest_api_types/ev_simulator/codec.hpp"
#include "nlohmann/json.hpp"
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <variant>

// ChargingCurve and SessionConfigParams are opted out of round-trip autogen via disable.csv:
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

TEST(ev_simulator, mode_of_maps_each_alternative) {
    EXPECT_EQ(mode_of(SessionConfigParams{AcIecSessionParams{}}), ChargeMode::AcIec);
    EXPECT_EQ(mode_of(SessionConfigParams{AcIso2SessionParams{}}), ChargeMode::AcIso2);
    EXPECT_EQ(mode_of(SessionConfigParams{AcIsoD20SessionParams{}}), ChargeMode::AcIsoD20);
    EXPECT_EQ(mode_of(SessionConfigParams{DcIso2SessionParams{}}), ChargeMode::DcIso2);
    EXPECT_EQ(mode_of(SessionConfigParams{DcIsoD20SessionParams{}}), ChargeMode::DcIsoD20);
}

TEST(ev_simulator, SessionConfigParams_roundtrip_ac_iso2_with_curve_and_optionals) {
    AcIso2SessionParams v;
    v.payment.emplace(PaymentOption::Contract);
    v.departure_time_s.emplace(3600);
    v.e_amount_wh.emplace(22000);
    v.charging_current_a.emplace(16.0f);
    v.three_phases.emplace(true);
    v.curve.emplace(make_valid_curve(true));
    SessionConfigParams original{v};

    auto ser = serialize(original);
    auto des = deserialize<SessionConfigParams>(ser);

    ASSERT_TRUE(std::holds_alternative<AcIso2SessionParams>(des));
    EXPECT_EQ(mode_of(original), mode_of(des));
    const auto& d = std::get<AcIso2SessionParams>(des);
    expect_opt_eq(v.payment, d.payment);
    expect_opt_eq(v.departure_time_s, d.departure_time_s);
    expect_opt_eq(v.e_amount_wh, d.e_amount_wh);
    ASSERT_TRUE(d.charging_current_a.has_value());
    EXPECT_FLOAT_EQ(v.charging_current_a.value(), d.charging_current_a.value());
    expect_opt_eq(v.three_phases, d.three_phases);
    ASSERT_TRUE(d.curve.has_value());
    expect_curve_eq(v.curve.value(), d.curve.value());
}

TEST(ev_simulator, SessionConfigParams_roundtrip_ac_iec_mode_only) {
    SessionConfigParams original{AcIecSessionParams{}};

    auto ser = serialize(original);
    auto des = deserialize<SessionConfigParams>(ser);

    ASSERT_TRUE(std::holds_alternative<AcIecSessionParams>(des));
    const auto& d = std::get<AcIecSessionParams>(des);
    EXPECT_FALSE(d.charging_current_a.has_value());
    EXPECT_FALSE(d.three_phases.has_value());
    EXPECT_FALSE(d.curve.has_value());
}

TEST(ev_simulator, SessionConfigParams_roundtrip_dc_iso_d20_bpt_and_mcs) {
    DcIsoD20SessionParams v;
    BptParams bpt;
    bpt.discharge_max_current_limit = 60.0f;
    bpt.discharge_max_power_limit = 11000.0f;
    bpt.discharge_target_current = 32.0f;
    bpt.discharge_minimal_soc = 20.0f;
    v.bpt.emplace(bpt);
    v.mcs_enabled = true;
    SessionConfigParams original{v};

    auto ser = serialize(original);
    auto des = deserialize<SessionConfigParams>(ser);

    ASSERT_TRUE(std::holds_alternative<DcIsoD20SessionParams>(des));
    const auto& d = std::get<DcIsoD20SessionParams>(des);
    ASSERT_TRUE(d.bpt.has_value());
    EXPECT_FLOAT_EQ(v.bpt->discharge_max_current_limit, d.bpt->discharge_max_current_limit);
    EXPECT_FLOAT_EQ(v.bpt->discharge_max_power_limit, d.bpt->discharge_max_power_limit);
    EXPECT_FLOAT_EQ(v.bpt->discharge_target_current, d.bpt->discharge_target_current);
    EXPECT_FLOAT_EQ(v.bpt->discharge_minimal_soc, d.bpt->discharge_minimal_soc);
    EXPECT_TRUE(d.mcs_enabled);
    EXPECT_FALSE(d.curve.has_value());
}

TEST(ev_simulator, SessionConfigParams_roundtrip_dc_iso2) {
    DcIso2SessionParams v;
    v.payment.emplace(PaymentOption::ExternalPayment);
    v.departure_time_s.emplace(1800);
    v.e_amount_wh.emplace(15000);
    SessionConfigParams original{v};

    auto des = deserialize<SessionConfigParams>(serialize(original));

    ASSERT_TRUE(std::holds_alternative<DcIso2SessionParams>(des));
    const auto& d = std::get<DcIso2SessionParams>(des);
    expect_opt_eq(v.payment, d.payment);
    expect_opt_eq(v.departure_time_s, d.departure_time_s);
    expect_opt_eq(v.e_amount_wh, d.e_amount_wh);
}

TEST(ev_simulator, SessionConfigParams_roundtrip_ac_iso_d20_bpt) {
    AcIsoD20SessionParams v;
    BptParams bpt;
    bpt.discharge_max_current_limit = 40.0f;
    bpt.discharge_max_power_limit = 7000.0f;
    bpt.discharge_target_current = 20.0f;
    bpt.discharge_minimal_soc = 15.0f;
    v.bpt.emplace(bpt);
    v.three_phases.emplace(true);
    SessionConfigParams original{v};

    auto des = deserialize<SessionConfigParams>(serialize(original));

    ASSERT_TRUE(std::holds_alternative<AcIsoD20SessionParams>(des));
    const auto& d = std::get<AcIsoD20SessionParams>(des);
    ASSERT_TRUE(d.bpt.has_value());
    EXPECT_FLOAT_EQ(v.bpt->discharge_target_current, d.bpt->discharge_target_current);
    expect_opt_eq(v.three_phases, d.three_phases);
}

TEST(ev_simulator, SessionConfigParams_unknown_mode_throws) {
    const std::string unknown = R"({"mode":"NotARealMode","params":{}})";
    EXPECT_THROW(deserialize<SessionConfigParams>(unknown), std::exception);
}

TEST(ev_simulator, SessionConfigParams_foreign_field_in_params_is_ignored) {
    // bpt has no meaning under AcIec; nlohmann ignores unknown keys, and the
    // AcIec alternative structurally cannot hold it.
    const std::string j = R"({"mode":"AcIec","params":{"charging_current_a":16.0,"bpt":{
        "discharge_max_current_limit":1.0,"discharge_max_power_limit":1.0,
        "discharge_target_current":1.0,"discharge_minimal_soc":1.0}}})";
    auto des = deserialize<SessionConfigParams>(j);
    ASSERT_TRUE(std::holds_alternative<AcIecSessionParams>(des));
    const auto& d = std::get<AcIecSessionParams>(des);
    ASSERT_TRUE(d.charging_current_a.has_value());
    EXPECT_FLOAT_EQ(16.0f, d.charging_current_a.value());
}
