// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include "ac_charge_parameter_discovery.hpp"
#include "ac_der_types.hpp"

#include <cstdint>
#include <optional>
#include <string>

#include <everest/util/vector/fixed_vector.hpp>

namespace iso15118::message_20 {

namespace datatypes {

// Type aliases for string-based XSD types
using InverterSwVersion = std::string;
using InverterHwVersion = std::string;
using InverterManufacturer = std::string;
using InverterModel = std::string;
using InverterSerialNumber = std::string;

// Enums for DER_SAE_AC_CPDResEnergyTransferMode
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

enum class PowerReference : std::uint8_t {
    MaximumActivePower,
    MomentaryPower,
};

// Structs for DER_SAE_AC_CPDReqEnergyTransferMode
struct EVApparentPowerLimits {
    RationalNumber maximum_apparent_power_during_charging_and_var_absorption;
    std::optional<RationalNumber> maximum_apparent_power_during_charging_and_var_absorption_L2;
    std::optional<RationalNumber> maximum_apparent_power_during_charging_and_var_absorption_L3;
    RationalNumber maximum_apparent_power_during_charging_and_var_injection;
    std::optional<RationalNumber> maximum_apparent_power_during_charging_and_var_injection_L2;
    std::optional<RationalNumber> maximum_apparent_power_during_charging_and_var_injection_L3;
    RationalNumber maximum_apparent_power_during_discharging_and_var_absorption;
    std::optional<RationalNumber> maximum_apparent_power_during_discharging_and_var_absorption_L2;
    std::optional<RationalNumber> maximum_apparent_power_during_discharging_and_var_absorption_L3;
    RationalNumber maximum_apparent_power_during_discharging_and_var_injection;
    std::optional<RationalNumber> maximum_apparent_power_during_discharging_and_var_injection_L2;
    std::optional<RationalNumber> maximum_apparent_power_during_discharging_and_var_injection_L3;
};

struct EVReactivePowerLimits {
    RationalNumber maximum_var_absorption_during_charging;
    std::optional<RationalNumber> maximum_var_absorption_during_charging_L2;
    std::optional<RationalNumber> maximum_var_absorption_during_charging_L3;
    std::optional<RationalNumber> minimum_var_absorption_during_charging;
    std::optional<RationalNumber> minimum_var_absorption_during_charging_L2;
    std::optional<RationalNumber> minimum_var_absorption_during_charging_L3;
    RationalNumber maximum_var_injection_during_charging;
    std::optional<RationalNumber> maximum_var_injection_during_charging_L2;
    std::optional<RationalNumber> maximum_var_injection_during_charging_L3;
    std::optional<RationalNumber> minimum_var_injection_during_charging;
    std::optional<RationalNumber> minimum_var_injection_during_charging_L2;
    std::optional<RationalNumber> minimum_var_injection_during_charging_L3;
    RationalNumber maximum_var_absorption_during_discharging;
    std::optional<RationalNumber> maximum_var_absorption_during_discharging_L2;
    std::optional<RationalNumber> maximum_var_absorption_during_discharging_L3;
    std::optional<RationalNumber> minimum_var_absorption_during_discharging;
    std::optional<RationalNumber> minimum_var_absorption_during_discharging_L2;
    std::optional<RationalNumber> minimum_var_absorption_during_discharging_L3;
    RationalNumber maximum_var_injection_during_discharging;
    std::optional<RationalNumber> maximum_var_injection_during_discharging_L2;
    std::optional<RationalNumber> maximum_var_injection_during_discharging_L3;
    std::optional<RationalNumber> minimum_var_injection_during_discharging;
    std::optional<RationalNumber> minimum_var_injection_during_discharging_L2;
    std::optional<RationalNumber> minimum_var_injection_during_discharging_L3;
    RationalNumber reactive_susceptance;
    std::optional<RationalNumber> reactive_susceptance_L2;
    std::optional<RationalNumber> reactive_susceptance_L3;
};

struct EVExcitationLimits {
    RationalNumber specified_over_excited_power_factor;
    std::optional<RationalNumber> specified_over_excited_power_factor_L2;
    std::optional<RationalNumber> specified_over_excited_power_factor_L3;
    RationalNumber specified_over_excited_discharge_power;
    std::optional<RationalNumber> specified_over_excited_discharge_power_L2;
    std::optional<RationalNumber> specified_over_excited_discharge_power_L3;
    RationalNumber specified_under_excited_power_factor;
    std::optional<RationalNumber> specified_under_excited_power_factor_L2;
    std::optional<RationalNumber> specified_under_excited_power_factor_L3;
    RationalNumber specified_under_excited_discharge_power;
    std::optional<RationalNumber> specified_under_excited_discharge_power_L2;
    std::optional<RationalNumber> specified_under_excited_discharge_power_L3;
};

struct EVInverterDetails {
    InverterSwVersion inverter_sw_version;
    std::optional<InverterHwVersion> inverter_hw_version;
    InverterManufacturer inverter_manufacturer;
    InverterModel inverter_model;
    InverterSerialNumber inverter_serial_number;
};

struct DER_SAE_AC_CPDReqEnergyTransferMode : AC_CPDReqEnergyTransferMode {
    Processing processing;
    RationalNumber maximum_discharge_power;
    std::optional<RationalNumber> maximum_discharge_power_L2;
    std::optional<RationalNumber> maximum_discharge_power_L3;
    std::optional<RationalNumber> minimum_discharge_power;
    std::optional<RationalNumber> minimum_discharge_power_L2;
    std::optional<RationalNumber> minimum_discharge_power_L3;
    std::optional<RationalNumber> session_total_discharge_energy_available;
    EVApparentPowerLimits apparent_power_limits;
    EVReactivePowerLimits reactive_power_limits;
    EVExcitationLimits excitation_limits;
    EVInverterDetails inverter_details;
    IEEE1547NormalCategory ieee1547_normal_category;
    IEEE1547AbnormalCategory ieee1547_abnormal_category;
    RationalNumber nominal_voltage;
    RationalNumber maximum_voltage;
    RationalNumber minimum_voltage;
    RationalNumber nominal_voltage_offset;
    bool j3072_certified;
    uint64_t j3072_certification_date;
    uint32_t useable_watt_hours;
    uint64_t update_time;
    uint32_t supported_modes;
    uint32_t enabled_modes;
};

// Structs for DER_SAE_AC_CPDResEnergyTransferMode
struct EVSEReactivePowerLimits {
    RationalNumber maximum_var_absorption_during_charging;
    std::optional<RationalNumber> maximum_var_absorption_during_charging_L2;
    std::optional<RationalNumber> maximum_var_absorption_during_charging_L3;
    RationalNumber maximum_var_injection_during_charging;
    std::optional<RationalNumber> maximum_var_injection_during_charging_L2;
    std::optional<RationalNumber> maximum_var_injection_during_charging_L3;
    RationalNumber maximum_var_absorption_during_discharging;
    std::optional<RationalNumber> maximum_var_absorption_during_discharging_L2;
    std::optional<RationalNumber> maximum_var_absorption_during_discharging_L3;
    RationalNumber maximum_var_injection_during_discharging;
    std::optional<RationalNumber> maximum_var_injection_during_discharging_L2;
    std::optional<RationalNumber> maximum_var_injection_during_discharging_L3;
};

struct GridLimits {
    RationalNumber nominal_frequency;
    RationalNumber nominal_voltage;
    RationalNumber nominal_voltage_offset;
    std::optional<RationalNumber> min_frequency;
    std::optional<RationalNumber> max_frequency;
    RationalNumber maximum_voltage;
    RationalNumber minimum_voltage;
};

// DERControlCPDRes and nested types
struct DataTuple {
    RationalNumber x_value;
    RationalNumber y_value;
};

constexpr auto CurveDataPointsMinLength = 2;
constexpr auto CurveDataPointsMaxLength = 10;
using CurveDataPointsList = everest::lib::util::fixed_vector<DataTuple, CurveDataPointsMaxLength>;

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

struct EnterServiceCPDRes {
    bool permit_service;
    RationalNumber enter_service_voltage_high;
    RationalNumber enter_service_voltage_low;
    RationalNumber enter_service_frequency_high;
    RationalNumber enter_service_frequency_low;
    std::optional<RationalNumber> enter_service_delay;
    std::optional<RationalNumber> enter_service_randomized_delay;
    std::optional<RationalNumber> enter_service_ramp_time;
};

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

struct ActivePowerSupportCPDRes {
    FrequencyDroop frequency_droop;
    VoltWatt volt_watt;
    ConstantWatt constant_watt;
    LimitMaxDischargePower limit_max_discharge_power;
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

struct ReactivePowerSupportCPDRes {
    ConstantPowerFactor constant_power_factor;
    VoltVar volt_var;
    WattVar watt_var;
    ConstantVar constant_var;
};

struct DERControlCPDRes {
    VoltageTrip voltage_trip;
    FrequencyTrip frequency_trip;
    EnterServiceCPDRes enter_service_cpd_res;
    ReactivePowerSupportCPDRes reactive_power_support_cpd_res;
    ActivePowerSupportCPDRes active_power_support_cpd_res;
};

struct DER_SAE_AC_CPDResEnergyTransferMode : AC_CPDResEnergyTransferMode {
    Processing processing;
    std::optional<EvseStatus> status;
    DERControlCPDRes der_control_cpd_res;
    std::optional<RationalNumber> nominal_charge_power;
    std::optional<RationalNumber> nominal_charge_power_L2;
    std::optional<RationalNumber> nominal_charge_power_L3;
    std::optional<RationalNumber> nominal_discharge_power;
    std::optional<RationalNumber> nominal_discharge_power_L2;
    std::optional<RationalNumber> nominal_discharge_power_L3;
    RationalNumber maximum_discharge_power;
    std::optional<RationalNumber> maximum_discharge_power_L2;
    std::optional<RationalNumber> maximum_discharge_power_L3;
    EVSEReactivePowerLimits reactive_power_limits;
    GridLimits grid_limits;
    RequiredDEROperatingMode required_der_operating_mode;
    GridConnectionMode grid_connection_mode;
    uint64_t update_time;
};

} // namespace datatypes

struct DER_SAE_AC_ChargeParameterDiscoveryRequest {
    Header header;
    datatypes::DER_SAE_AC_CPDReqEnergyTransferMode transfer_mode;
};

struct DER_SAE_AC_ChargeParameterDiscoveryResponse {
    Header header;
    datatypes::ResponseCode response_code{};
    datatypes::DER_SAE_AC_CPDResEnergyTransferMode transfer_mode;
};

} // namespace iso15118::message_20
