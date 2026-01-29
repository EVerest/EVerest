// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>

#include "ac_charge_loop.hpp"
#include "ac_der_sae_types.hpp"

namespace iso15118::message_20 {

namespace datatypes {
namespace sae {

struct EVApparentPower {
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

struct EVReactivePower {
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
};

struct EVExcitation {
    std::optional<RationalNumber> specified_over_excited_power_factor;
    std::optional<RationalNumber> specified_over_excited_power_factor_L2;
    std::optional<RationalNumber> specified_over_excited_power_factor_L3;
    RationalNumber specified_over_excited_discharge_power;
    std::optional<RationalNumber> specified_over_excited_discharge_power_L2;
    std::optional<RationalNumber> specified_over_excited_discharge_power_L3;
    std::optional<RationalNumber> specified_under_excited_power_factor;
    std::optional<RationalNumber> specified_under_excited_power_factor_L2;
    std::optional<RationalNumber> specified_under_excited_power_factor_L3;
    RationalNumber specified_under_excited_discharge_power;
    std::optional<RationalNumber> specified_under_excited_discharge_power_L2;
    std::optional<RationalNumber> specified_under_excited_discharge_power_L3;
};

struct DER_Scheduled_AC_CLReqControlMode : Scheduled_AC_CLReqControlMode {
    RationalNumber present_voltage;
    RationalNumber present_frequency;
    std::optional<RationalNumber> maximum_discharge_power;
    std::optional<RationalNumber> maximum_discharge_power_L2;
    std::optional<RationalNumber> maximum_discharge_power_L3;
    std::optional<RationalNumber> minimum_discharge_power;
    std::optional<RationalNumber> minimum_discharge_power_L2;
    std::optional<RationalNumber> minimum_discharge_power_L3;
    DEROperationalState der_operational_state;
    DERConnectionStatus der_connection_status;
    std::optional<EVApparentPower> apparent_power;
    std::optional<EVReactivePower> reactive_power;
    std::optional<EVExcitation> excitation;
    uint64_t update_time;
    std::optional<uint32_t> minimum_charging_duration;
    std::optional<uint32_t> duration_maximum_charge_rate;
    std::optional<uint32_t> duration_maximum_discharge_rate;
    uint32_t der_alarm_status;
    uint32_t enabled_modes;
};

struct DER_Dynamic_AC_CLReqControlMode : Dynamic_AC_CLReqControlMode {
    RationalNumber maximum_discharge_power;
    std::optional<RationalNumber> maximum_discharge_power_L2;
    std::optional<RationalNumber> maximum_discharge_power_L3;
    RationalNumber minimum_discharge_power;
    std::optional<RationalNumber> minimum_discharge_power_L2;
    std::optional<RationalNumber> minimum_discharge_power_L3;
    RationalNumber present_voltage;
    RationalNumber present_frequency;
    std::optional<RationalNumber> session_total_discharge_energy_available;
    std::optional<EVApparentPower> apparent_power;
    std::optional<EVReactivePower> reactive_power;
    std::optional<EVExcitation> excitation;
    std::optional<RationalNumber> maximum_v2x_energy_request;
    std::optional<RationalNumber> minimum_v2x_energy_request;
    DEROperationalState der_operational_state;
    DERConnectionStatus der_connection_status;
    uint64_t update_time;
    uint32_t minimum_charging_duration;
    uint32_t duration_maximum_charge_rate;
    uint32_t duration_maximum_discharge_rate;
    uint32_t der_alarm_status;
    uint32_t enabled_modes;
};

struct EnterServiceCLRes {
    bool permit_service;
    std::optional<RationalNumber> enter_service_voltage_high;
    std::optional<RationalNumber> enter_service_voltage_low;
    std::optional<RationalNumber> enter_service_frequency_high;
    std::optional<RationalNumber> enter_service_frequency_low;
    std::optional<RationalNumber> enter_service_delay;
    std::optional<RationalNumber> enter_service_randomized_delay;
    std::optional<RationalNumber> enter_service_ramp_time;
};

struct ReactivePowerSupportCLRes {
    std::optional<ConstantPowerFactor> constant_power_factor;
    std::optional<VoltVar> volt_var;
    std::optional<WattVar> watt_var;
    std::optional<ConstantVar> constant_var;
};

struct ActivePowerSupportCLRes {
    std::optional<FrequencyDroop> frequency_droop;
    std::optional<VoltWatt> volt_watt;
    std::optional<ConstantWatt> constant_watt;
    std::optional<LimitMaxDischargePower> limit_max_discharge_power;
};

struct DERControlCLRes {
    std::optional<VoltageTrip> voltage_trip;
    std::optional<FrequencyTrip> frequency_trip;
    EnterServiceCLRes enter_service_cl_res;
    std::optional<ReactivePowerSupportCLRes> reactive_power_support_cl_res;
    std::optional<ActivePowerSupportCLRes> active_power_support_cl_res;
};

struct DER_Scheduled_AC_CLResControlMode : Scheduled_AC_CLResControlMode {
    DERControlCLRes der_control_cl_res;
    std::optional<RationalNumber> evse_maximum_charge_power;
    std::optional<RationalNumber> evse_maximum_charge_power_L2;
    std::optional<RationalNumber> evse_maximum_charge_power_L3;
    std::optional<RationalNumber> evse_maximum_discharge_power;
    std::optional<RationalNumber> evse_maximum_discharge_power_L2;
    std::optional<RationalNumber> evse_maximum_discharge_power_L3;
    std::optional<RequiredDEROperatingMode> required_der_operating_mode;
    std::optional<GridConnectionMode> grid_connection_mode;
};

struct DER_Dynamic_AC_CLResControlMode : Dynamic_AC_CLResControlMode {
    DERControlCLRes der_control_cl_res;
    std::optional<RationalNumber> evse_maximum_charge_power;
    std::optional<RationalNumber> evse_maximum_charge_power_L2;
    std::optional<RationalNumber> evse_maximum_charge_power_L3;
    std::optional<RationalNumber> evse_maximum_discharge_power;
    std::optional<RationalNumber> evse_maximum_discharge_power_L2;
    std::optional<RationalNumber> evse_maximum_discharge_power_L3;
    std::optional<RequiredDEROperatingMode> required_der_operating_mode;
    std::optional<GridConnectionMode> grid_connection_mode;
};

} // namespace sae
} // namespace datatypes

struct DER_SAE_AC_ChargeLoopRequest {

    Header header;

    // the following 2 are inherited from ChargeLoopReq
    std::optional<datatypes::DisplayParameters> display_parameters;
    bool meter_info_requested;

    std::variant<datatypes::sae::DER_Scheduled_AC_CLReqControlMode, datatypes::sae::DER_Dynamic_AC_CLReqControlMode>
        control_mode;
};

struct DER_SAE_AC_ChargeLoopResponse {

    Header header;
    datatypes::ResponseCode response_code;

    // the following 3 are inherited from ChargeLoopRes
    std::optional<datatypes::EvseStatus> status;
    std::optional<datatypes::MeterInfo> meter_info;
    std::optional<datatypes::Receipt> receipt;

    std::optional<datatypes::RationalNumber> target_frequency;

    std::variant<datatypes::sae::DER_Scheduled_AC_CLResControlMode, datatypes::sae::DER_Dynamic_AC_CLResControlMode>
        control_mode{datatypes::sae::DER_Scheduled_AC_CLResControlMode()};
};

} // namespace iso15118::message_20
