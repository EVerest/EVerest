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

namespace iso15118::sae {

// LSB
enum class DerBitMapFunctions : std::uint8_t {
    ChargeFunction = 0,
    DischargeFunction = 1,
    EnterService = 3,
    ConstantPowerFactorUnderExcitedFunction = 4,
    ConstantPowerFactorOverExcitedFunction = 5,
    ConstantReactivePowerFunction = 6,
    ConstantActivePowerFunction = 7,
    FrequencyDroopFunction = 8,
    HighFrequencyMayTripFunction = 10,
    HighFrequencyMustTripFunction = 11,
    HighVoltageMayTripFunction = 12,
    HighVoltageMomentaryCessationFunction = 13,
    HighVoltageMustTripFunction = 14,
    LowFrequencyMayTripFunction = 15,
    LowFrequencyMustTripFunction = 16,
    LowVoltageMayTripFunction = 17,
    LowVoltageMomentaryCessationFunction = 18,
    LowVoltageMustTripFunction = 19,
    LimitMaximumActiveDischargePowerFunction = 20,
    EVSETargetReactivePowerFunction = 21,
    EVSETargetActivePowerFunction = 22,
    VoltVarFunction = 23,
    VoltWattFunction = 24,
    WattVarFunction = 26,
};

enum class RequiredDEROperatingMode : std::uint8_t {
    GridFollowing,
    GridForming,
};

enum class GridConnectionMode : std::uint8_t {
    GridConnected,
    GridIslanded,
};

enum class DERUnit : std::uint8_t {
    V,
    Hz,
    W,
    s,
    var,
    PercentageEVMaximumConfiguredActivePower,
    PercentageEVMaximumConfiguredReactivePower,
    PercentageEVMaximumConfiguredApparentPower,
    PercentageEVMaximumAvailableActivePower,
    PercentageEVMaximumAvailableReactivePower,
    PercentageV,
};

enum class PowerFactorExcitation : std::uint8_t {
    OverExcited,
    UnderExcited
};

enum class PowerReference : std::uint8_t {
    MaximumActivePower,
    MomentaryPower,
};

struct DataTuple {
    float x_value{0.0f};
    float y_value{0.0f};
};

constexpr auto CurveDataPointsMaxLength = 10;
using CurveDataPointsList = everest::lib::util::fixed_vector<DataTuple, CurveDataPointsMaxLength>;

struct DERCurve {
    bool enable{false};
    std::optional<uint16_t> priority;
    DERUnit x_unit{DERUnit::PercentageV};
    DERUnit y_unit{DERUnit::s};
    CurveDataPointsList curve_data_points;
    std::optional<CurveDataPointsList> curve_data_points_L2;
    std::optional<CurveDataPointsList> curve_data_points_L3;
};

struct ConstantPowerFactor {
    bool enable{false};
    std::optional<uint16_t> priority;
    float power_factor_value{1.0f};
    std::optional<float> power_factor_value_L2;
    std::optional<float> power_factor_value_L3;
    PowerFactorExcitation power_factor_excitation{PowerFactorExcitation::OverExcited};
    std::optional<PowerFactorExcitation> power_factor_excitation_L2;
    std::optional<PowerFactorExcitation> power_factor_excitation_L3;
};

struct VoltVar {
    bool enable{false};
    std::optional<uint16_t> priority;
    DERUnit x_unit{DERUnit::PercentageV};
    DERUnit y_unit{DERUnit::PercentageEVMaximumConfiguredReactivePower};
    CurveDataPointsList curve_data_points;
    std::optional<CurveDataPointsList> curve_data_points_L2;
    std::optional<CurveDataPointsList> curve_data_points_L3;
    float open_loop_response_time{0.0f};
    std::optional<uint32_t> time_constant_pt1;
    float reference_voltage{0.0f};
    bool autonomous_reference_voltage_adjustment_enable{false};
    uint32_t reference_voltage_adjustment_time_constant{0};
};

struct WattVar {
    bool enable{false};
    std::optional<uint16_t> priority;
    DERUnit x_unit{DERUnit::PercentageEVMaximumConfiguredActivePower};
    DERUnit y_unit{DERUnit::PercentageEVMaximumConfiguredReactivePower};
    CurveDataPointsList curve_data_points;
    std::optional<CurveDataPointsList> curve_data_points_L2;
    std::optional<CurveDataPointsList> curve_data_points_L3;
    std::optional<float> open_loop_response_time;
    std::optional<uint32_t> time_constant_pt1;
};

struct ConstantVar {
    bool enable{false};
    std::optional<uint16_t> priority;
    float var_setpoint{0.0f};
    std::optional<float> var_setpoint_L2;
    std::optional<float> var_setpoint_L3;
    DERUnit unit{DERUnit::PercentageEVMaximumConfiguredReactivePower};
};

struct FrequencyDroopSettings {
    float db{0.0f};
    float droop_factor{0.0f};
    std::optional<float> droop_factor_L2;
    std::optional<float> droop_factor_L3;
    PowerReference power_reference{PowerReference::MaximumActivePower};
    std::optional<PowerReference> power_reference_L2;
    std::optional<PowerReference> power_reference_L3;
    float open_loop_response_time{0.0f};
};

struct FrequencyDroop {
    bool enable{false};
    std::optional<uint16_t> priority;
    std::optional<FrequencyDroopSettings> over_frequency_droop;
    std::optional<FrequencyDroopSettings> under_frequency_droop;
};

struct VoltWatt {
    bool enable{false};
    std::optional<uint16_t> priority;
    DERUnit x_unit{DERUnit::PercentageV};
    DERUnit y_unit{DERUnit::PercentageEVMaximumConfiguredActivePower};
    CurveDataPointsList curve_data_points;
    std::optional<CurveDataPointsList> curve_data_points_L2;
    std::optional<CurveDataPointsList> curve_data_points_L3;
    float open_loop_response_time{0.0f};
    std::optional<uint32_t> time_constant_pt1;
};

struct ConstantWatt {
    bool enable{false};
    std::optional<uint16_t> priority;
    float watt_setpoint{0.0f};
    std::optional<float> watt_setpoint_L2;
    std::optional<float> watt_setpoint_L3;
    DERUnit unit{DERUnit::PercentageEVMaximumConfiguredActivePower};
};

struct LimitMaxDischargePower {
    bool enable{false};
    std::optional<uint16_t> priority;
    uint16_t percentage_value{100};
    std::optional<uint16_t> percentage_value_L2;
    std::optional<uint16_t> percentage_value_L3;
    std::optional<float> open_loop_response_time;
};

struct VoltageTrip {
    DERCurve over_voltage_must_trip_curve;
    DERCurve under_voltage_must_trip_curve;
    std::optional<DERCurve> over_voltage_momentary_cessation_trip_curve;
    std::optional<DERCurve> under_voltage_momentary_cessation_trip_curve;
    std::optional<DERCurve> over_voltage_may_trip_curve;
    std::optional<DERCurve> under_voltage_may_trip_curve;
};

struct FrequencyTrip {
    DERCurve over_frequency_must_trip_curve;
    DERCurve under_frequency_must_trip_curve;
    std::optional<DERCurve> over_frequency_may_trip_curve;
    std::optional<DERCurve> under_frequency_may_trip_curve;
};

// TODO(SL): Check 3363, 3364, 3365
struct EnterServiceCPDRes {
    bool permit_service{false};
    float enter_service_voltage_high{0.0f};
    float enter_service_voltage_low{0.0f};
    float enter_service_frequency_high{0.0f};
    float enter_service_frequency_low{0.0f};
    std::optional<float> enter_service_delay;
    std::optional<float> enter_service_randomized_delay;
    std::optional<float> enter_service_ramp_time;
};

struct ReactivePowerSupportCPDRes {
    ConstantPowerFactor constant_power_factor;
    VoltVar volt_var;
    WattVar watt_var;
    ConstantVar constant_var;
};

struct ActivePowerSupportCPDRes {
    FrequencyDroop frequency_droop;
    VoltWatt volt_watt;
    ConstantWatt constant_watt;
    LimitMaxDischargePower limit_max_discharge_power;
};

struct DERControl {
    VoltageTrip voltage_trip;
    FrequencyTrip frequency_trip;
    EnterServiceCPDRes enter_service;
    ReactivePowerSupportCPDRes reactive_power_support;
    ActivePowerSupportCPDRes active_power_support;
};

} // namespace iso15118::sae
