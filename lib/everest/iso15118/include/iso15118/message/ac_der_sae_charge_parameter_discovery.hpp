// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include "ac_charge_parameter_discovery.hpp"
#include "ac_der_sae_types.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace iso15118::message_20 {

namespace datatypes {
namespace sae {

// Type aliases for string-based XSD types
using InverterSwVersion = std::string;
using InverterHwVersion = std::string;
using InverterManufacturer = std::string;
using InverterModel = std::string;
using InverterSerialNumber = std::string;

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

struct ActivePowerSupportCPDRes {
    FrequencyDroop frequency_droop;
    VoltWatt volt_watt;
    ConstantWatt constant_watt;
    LimitMaxDischargePower limit_max_discharge_power;
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

} // namespace sae
} // namespace datatypes

struct DER_SAE_AC_ChargeParameterDiscoveryRequest {
    Header header;
    datatypes::sae::DER_SAE_AC_CPDReqEnergyTransferMode transfer_mode;
};

struct DER_SAE_AC_ChargeParameterDiscoveryResponse {
    Header header;
    datatypes::ResponseCode response_code{};
    datatypes::sae::DER_SAE_AC_CPDResEnergyTransferMode transfer_mode;
};

} // namespace iso15118::message_20
