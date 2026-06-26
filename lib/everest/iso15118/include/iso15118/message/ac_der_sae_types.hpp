// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>

#include <everest/util/vector/fixed_vector.hpp>

#include "common_types.hpp"

namespace iso15118::message_20::datatypes::sae {

enum class PowerFactorExcitation : std::uint8_t {
    OverExcited,
    UnderExcited
};

enum class IEEE1547NormalCategory : std::uint8_t {
    CategoryA,
    CategoryB,
};

enum class IEEE1547AbnormalCategory : std::uint8_t {
    CategoryI,
    CategoryII,
    CategoryIII,
};

enum class DEROperationalState : std::uint8_t {
    On,
    Off,
};

enum class DERConnectionStatus : std::uint8_t {
    Disconnected,
    Connected,
};

// Enums and types for DER power support
enum class PowerReference : std::uint8_t {
    MaximumActivePower,
    MomentaryPower,
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

struct DataTuple {
    RationalNumber x_value;
    RationalNumber y_value;
};

constexpr auto CurveDataPointsMinLength = 2;
constexpr auto CurveDataPointsMaxLength = 10;
using CurveDataPointsList = everest::lib::util::fixed_vector<DataTuple, CurveDataPointsMaxLength>;

// Shared structs for DER types
struct FrequencyDroopSettings {
    RationalNumber db;
    RationalNumber droop_factor;
    std::optional<RationalNumber> droop_factor_L2;
    std::optional<RationalNumber> droop_factor_L3;
    PowerReference power_reference;
    std::optional<PowerReference> power_reference_L2;
    std::optional<PowerReference> power_reference_L3;
    RationalNumber open_loop_response_time;
};

struct FrequencyDroop {
    bool enable;
    std::optional<uint16_t> priority;
    std::optional<FrequencyDroopSettings> over_frequency_droop;
    std::optional<FrequencyDroopSettings> under_frequency_droop;
};

struct VoltWatt {
    bool enable;
    std::optional<uint16_t> priority;
    DERUnit x_unit;
    DERUnit y_unit;
    CurveDataPointsList curve_data_points;
    std::optional<CurveDataPointsList> curve_data_points_L2;
    std::optional<CurveDataPointsList> curve_data_points_L3;
    RationalNumber open_loop_response_time;
    std::optional<uint32_t> time_constant_pt1;
};

struct ConstantWatt {
    bool enable;
    std::optional<uint16_t> priority;
    RationalNumber watt_setpoint;
    std::optional<RationalNumber> watt_setpoint_L2;
    std::optional<RationalNumber> watt_setpoint_L3;
    DERUnit unit;
};

struct LimitMaxDischargePower {
    bool enable;
    std::optional<uint16_t> priority;
    uint16_t percentage_value;
    std::optional<uint16_t> percentage_value_L2;
    std::optional<uint16_t> percentage_value_L3;
    std::optional<RationalNumber> open_loop_response_time;
};

struct ConstantPowerFactor {
    bool enable;
    std::optional<uint16_t> priority;
    RationalNumber power_factor_value;
    std::optional<RationalNumber> power_factor_value_L2;
    std::optional<RationalNumber> power_factor_value_L3;
    PowerFactorExcitation power_factor_excitation;
    std::optional<PowerFactorExcitation> power_factor_excitation_L2;
    std::optional<PowerFactorExcitation> power_factor_excitation_L3;
};

struct WattVar {
    bool enable;
    std::optional<uint16_t> priority;
    DERUnit x_unit;
    DERUnit y_unit;
    CurveDataPointsList curve_data_points;
    std::optional<CurveDataPointsList> curve_data_points_L2;
    std::optional<CurveDataPointsList> curve_data_points_L3;
    std::optional<RationalNumber> open_loop_response_time;
    std::optional<uint32_t> time_constant_pt1;
};

struct VoltVar {
    bool enable;
    std::optional<uint16_t> priority;
    DERUnit x_unit;
    DERUnit y_unit;
    CurveDataPointsList curve_data_points;
    std::optional<CurveDataPointsList> curve_data_points_L2;
    std::optional<CurveDataPointsList> curve_data_points_L3;
    RationalNumber open_loop_response_time;
    std::optional<uint32_t> time_constant_pt1;
    RationalNumber reference_voltage;
    bool autonomous_reference_voltage_adjustment_enable;
    uint32_t reference_voltage_adjustment_time_constant;
};

struct ConstantVar {
    bool enable;
    std::optional<uint16_t> priority;
    RationalNumber var_setpoint;
    std::optional<RationalNumber> var_setpoint_L2;
    std::optional<RationalNumber> var_setpoint_L3;
    DERUnit unit;
};

// Enums for DER_SAE_AC_CPDResEnergyTransferMode
enum class RequiredDEROperatingMode : std::uint8_t {
    GridFollowing,
    GridForming,
};

enum class GridConnectionMode : std::uint8_t {
    GridConnected,
    GridIslanded,
};

// DERCurve struct used by VoltageTrip and FrequencyTrip
struct DERCurve {
    bool enable;
    std::optional<uint16_t> priority;
    DERUnit x_unit;
    DERUnit y_unit;
    CurveDataPointsList curve_data_points;
    std::optional<CurveDataPointsList> curve_data_points_L2;
    std::optional<CurveDataPointsList> curve_data_points_L3;
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

} // namespace iso15118::message_20::datatypes::sae
