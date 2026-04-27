// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <variant>

#include <everest/util/vector/fixed_vector.hpp>

namespace iso15118::iec {

enum class OperatingMode : std::uint8_t {
    GridFollowing = 0,
    GridForming,
};

enum class GridConnectionMode : std::uint8_t {
    GridConnected = 0,
    GridIslanded
};

enum class DERControlName : std::uint8_t {
    OverFrequencyWattMode = 0,
    UnderFrequencyWattMode,
    VoltWattMode,
    VoltVarMode,
    WattVarMode,
    WattCosPhiMode,
    DSOQSetpointProvision,
    DSOCosPhiSetpointProvision,
    DCInjectionRestriction,
    ZeroCurrentMode,
    OverVoltageFaultRideThroughMode,
    UnderVoltageFaultRideThroughMode
};

using MaximumLevelDCInjection = float;

enum class PowerReference : std::uint8_t {
    MaximumDischargePower = 0,
    MomentaryPower
};

enum class CurveDataPointsUnit : std::uint8_t {
    V = 0,
    Hz,
    W,
    s,
    var,
};

enum class PowerFactorExcitation : std::uint8_t {
    OverExcited = 0,
    UnderExcited,
};

enum class LockValueUnit : std::uint8_t {
    V = 0,
    Hz,
    W,
    s,
    var,
};

struct FrequencyWatt {
    float fstart;
    float fstop;
    std::optional<uint16_t> intentional_delay_fstop;
    float slope;
    std::optional<uint16_t> deactivation_time;
    std::optional<uint16_t> intentional_delay_power_control;
    PowerReference power_reference;
    bool hysteresis_control;
    std::optional<uint16_t> power_up_ramp;
    bool pt1_response_active_power;
    float step_response_time_constant_active_power;
};

struct VoltWatt {
    PowerReference power_reference;
    float u_start;
    float u_stop;
    bool pt1_response_active_power;
    float step_response_time_constant_active_power;
    std::optional<uint32_t> intentional_delay_power_control;
};

struct ActivePowerSupport {
    std::optional<FrequencyWatt> under_frequency_watt;
    std::optional<FrequencyWatt> over_frequency_watt;
    std::optional<VoltWatt> volt_watt;
};

struct SetpointExcitation {
    float setpoint_value;
    std::optional<PowerFactorExcitation> excitation;
};

struct DataTuple {
    float x_value;
    SetpointExcitation y_value;
};

constexpr auto CurveDataPointsMaxLength = 10;
using CurveDataPointsList = everest::lib::util::fixed_vector<DataTuple, CurveDataPointsMaxLength>;

struct DERCurve {
    CurveDataPointsUnit x_unit;
    CurveDataPointsUnit y_unit;
    CurveDataPointsList curve_data_points;
    std::optional<float> min_cos_phi;
    std::optional<LockValueUnit> lock_value_unit;
    std::optional<float> lock_in_value;
    std::optional<float> lock_out_value;
    bool pt1_response_reactive_power;
    float step_response_time_constant_reactive_power;
    std::optional<float> intentional_delay;
};

struct ReactivePowerSupport {
    enum class Mode : uint8_t {
        VoltVar,
        WattVar,
        WattCosPhi
    };
    Mode mode;
    DERCurve curve;
};

struct DSOQSetpoint {
    float dso_q_setpoint_value;
    std::optional<float> dso_q_setpoint_value_l2;
    std::optional<float> dso_q_setpoint_value_l3;
    bool pt1_response_reactive_power;
    float step_response_time_constant_reactive_power;
};

struct DSOCosPhiSetpoint {
    float dso_cos_phi_setpoint_value;
    std::optional<float> dso_cos_phi_setpoint_value_l2;
    std::optional<float> dso_cos_phi_setpoint_value_l3;
    PowerFactorExcitation excitation;
    bool pt1_response_reactive_power;
    float step_response_time_constant_reactive_power;
};

struct ZeroCurrent {
    std::optional<float> over_voltage_limit;
    std::optional<float> under_voltage_limit;
    std::optional<float> over_voltage_recovery_limit;
    std::optional<float> under_voltage_recovery_limit;
    bool pt1_response_active_power;
    float step_response_time_constant_active_power;
    bool pt1_response_reactive_power;
    float step_response_time_constant_reactive_power;
};

struct FaultRideThrough {
    float voltage_limit_start_frt;
    std::optional<float> voltage_limit_stop_frt;
    std::optional<float> voltage_recovery_limit;
    std::optional<float> voltage_ride_through_positive_curve_k_factor;
    std::optional<float> voltage_ride_through_negative_curve_k_factor;
    bool pt1_response_active_power;
    float step_response_time_constant_active_power;
    bool pt1_response_reactive_power;
    float step_response_time_constant_reactive_power;
};

using DERControlFunction = std::variant<FrequencyWatt, VoltWatt, DERCurve, DSOQSetpoint, DSOCosPhiSetpoint,
                                        MaximumLevelDCInjection, ZeroCurrent, FaultRideThrough>;
} // namespace iso15118::iec
