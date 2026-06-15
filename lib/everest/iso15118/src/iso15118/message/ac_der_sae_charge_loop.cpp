// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/message/ac_der_sae_charge_loop.hpp>

#include <iso15118/detail/variant_access.hpp>

#include <cbv2g/iso_20/iso20_AC_DER_SAE_Decoder.h>
#include <cbv2g/iso_20/iso20_AC_DER_SAE_Encoder.h>

namespace iso15118::message_20 {

template <> void convert(const struct iso20_ac_der_sae_DisplayParametersType& in, datatypes::DisplayParameters& out) {
    CB2CPP_ASSIGN_IF_USED(in.PresentSOC, out.present_soc);
    CB2CPP_ASSIGN_IF_USED(in.MinimumSOC, out.min_soc);
    CB2CPP_ASSIGN_IF_USED(in.TargetSOC, out.target_soc);
    CB2CPP_ASSIGN_IF_USED(in.MaximumSOC, out.max_soc);

    CB2CPP_ASSIGN_IF_USED(in.RemainingTimeToMinimumSOC, out.remaining_time_to_min_soc);
    CB2CPP_ASSIGN_IF_USED(in.RemainingTimeToTargetSOC, out.remaining_time_to_target_soc);
    CB2CPP_ASSIGN_IF_USED(in.RemainingTimeToMaximumSOC, out.remaining_time_to_max_soc);

    CB2CPP_ASSIGN_IF_USED(in.ChargingComplete, out.charging_complete);
    CB2CPP_CONVERT_IF_USED(in.BatteryEnergyCapacity, out.battery_energy_capacity);
    CB2CPP_ASSIGN_IF_USED(in.InletHot, out.inlet_hot);
}

template <> void convert(const datatypes::DisplayParameters& in, struct iso20_ac_der_sae_DisplayParametersType& out) {
    init_iso20_ac_der_sae_DisplayParametersType(&out);

    CPP2CB_ASSIGN_IF_USED(in.present_soc, out.PresentSOC);
    CPP2CB_ASSIGN_IF_USED(in.min_soc, out.MinimumSOC);
    CPP2CB_ASSIGN_IF_USED(in.target_soc, out.TargetSOC);
    CPP2CB_ASSIGN_IF_USED(in.max_soc, out.MaximumSOC);

    CPP2CB_ASSIGN_IF_USED(in.remaining_time_to_min_soc, out.RemainingTimeToMinimumSOC);
    CPP2CB_ASSIGN_IF_USED(in.remaining_time_to_target_soc, out.RemainingTimeToTargetSOC);
    CPP2CB_ASSIGN_IF_USED(in.remaining_time_to_max_soc, out.RemainingTimeToMaximumSOC);

    CPP2CB_ASSIGN_IF_USED(in.charging_complete, out.ChargingComplete);
    CPP2CB_CONVERT_IF_USED(in.battery_energy_capacity, out.BatteryEnergyCapacity);
    CPP2CB_ASSIGN_IF_USED(in.inlet_hot, out.InletHot);
}

template <> void convert(const struct iso20_ac_der_sae_EVApparentPowerType& in, datatypes::EVApparentPower& out) {
    convert(in.EVMaximumApparentPowerDuringChargingAndVarAbsorption,
            out.maximum_apparent_power_during_charging_and_var_absorption);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumApparentPowerDuringChargingAndVarAbsorption_L2,
                           out.maximum_apparent_power_during_charging_and_var_absorption_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumApparentPowerDuringChargingAndVarAbsorption_L3,
                           out.maximum_apparent_power_during_charging_and_var_absorption_L3);
    convert(in.EVMaximumApparentPowerDuringChargingAndVarInjection,
            out.maximum_apparent_power_during_charging_and_var_injection);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumApparentPowerDuringChargingAndVarInjection_L2,
                           out.maximum_apparent_power_during_charging_and_var_injection_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumApparentPowerDuringChargingAndVarInjection_L3,
                           out.maximum_apparent_power_during_charging_and_var_injection_L3);
    convert(in.EVMaximumApparentPowerDuringDischargingAndVarAbsorption,
            out.maximum_apparent_power_during_discharging_and_var_absorption);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumApparentPowerDuringDischargingAndVarAbsorption_L2,
                           out.maximum_apparent_power_during_discharging_and_var_absorption_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumApparentPowerDuringDischargingAndVarAbsorption_L3,
                           out.maximum_apparent_power_during_discharging_and_var_absorption_L3);
    convert(in.EVMaximumApparentPowerDuringDischargingAndVarInjection,
            out.maximum_apparent_power_during_discharging_and_var_injection);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumApparentPowerDuringDischargingAndVarInjection_L2,
                           out.maximum_apparent_power_during_discharging_and_var_injection_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumApparentPowerDuringDischargingAndVarInjection_L3,
                           out.maximum_apparent_power_during_discharging_and_var_injection_L3);
}

template <> void convert(const datatypes::EVApparentPower& in, struct iso20_ac_der_sae_EVApparentPowerType& out) {
    init_iso20_ac_der_sae_EVApparentPowerType(&out);
    convert(in.maximum_apparent_power_during_charging_and_var_absorption,
            out.EVMaximumApparentPowerDuringChargingAndVarAbsorption);
    CPP2CB_CONVERT_IF_USED(in.maximum_apparent_power_during_charging_and_var_absorption_L2,
                           out.EVMaximumApparentPowerDuringChargingAndVarAbsorption_L2);
    CPP2CB_CONVERT_IF_USED(in.maximum_apparent_power_during_charging_and_var_absorption_L3,
                           out.EVMaximumApparentPowerDuringChargingAndVarAbsorption_L3);
    convert(in.maximum_apparent_power_during_charging_and_var_injection,
            out.EVMaximumApparentPowerDuringChargingAndVarInjection);
    CPP2CB_CONVERT_IF_USED(in.maximum_apparent_power_during_charging_and_var_injection_L2,
                           out.EVMaximumApparentPowerDuringChargingAndVarInjection_L2);
    CPP2CB_CONVERT_IF_USED(in.maximum_apparent_power_during_charging_and_var_injection_L3,
                           out.EVMaximumApparentPowerDuringChargingAndVarInjection_L3);
    convert(in.maximum_apparent_power_during_discharging_and_var_absorption,
            out.EVMaximumApparentPowerDuringDischargingAndVarAbsorption);
    CPP2CB_CONVERT_IF_USED(in.maximum_apparent_power_during_discharging_and_var_absorption_L2,
                           out.EVMaximumApparentPowerDuringDischargingAndVarAbsorption_L2);
    CPP2CB_CONVERT_IF_USED(in.maximum_apparent_power_during_discharging_and_var_absorption_L3,
                           out.EVMaximumApparentPowerDuringDischargingAndVarAbsorption_L3);
    convert(in.maximum_apparent_power_during_discharging_and_var_injection,
            out.EVMaximumApparentPowerDuringDischargingAndVarInjection);
    CPP2CB_CONVERT_IF_USED(in.maximum_apparent_power_during_discharging_and_var_injection_L2,
                           out.EVMaximumApparentPowerDuringDischargingAndVarInjection_L2);
    CPP2CB_CONVERT_IF_USED(in.maximum_apparent_power_during_discharging_and_var_injection_L3,
                           out.EVMaximumApparentPowerDuringDischargingAndVarInjection_L3);
}

template <> void convert(const struct iso20_ac_der_sae_EVReactivePowerType& in, datatypes::EVReactivePower& out) {
    convert(in.EVMaximumVarAbsorptionDuringCharging, out.maximum_var_absorption_during_charging);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumVarAbsorptionDuringCharging_L2, out.maximum_var_absorption_during_charging_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumVarAbsorptionDuringCharging_L3, out.maximum_var_absorption_during_charging_L3);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumVarAbsorptionDuringCharging, out.minimum_var_absorption_during_charging);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumVarAbsorptionDuringCharging_L2, out.minimum_var_absorption_during_charging_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumVarAbsorptionDuringCharging_L3, out.minimum_var_absorption_during_charging_L3);
    convert(in.EVMaximumVarInjectionDuringCharging, out.maximum_var_injection_during_charging);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumVarInjectionDuringCharging_L2, out.maximum_var_injection_during_charging_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumVarInjectionDuringCharging_L3, out.maximum_var_injection_during_charging_L3);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumVarInjectionDuringCharging, out.minimum_var_injection_during_charging);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumVarInjectionDuringCharging_L2, out.minimum_var_injection_during_charging_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumVarInjectionDuringCharging_L3, out.minimum_var_injection_during_charging_L3);
    convert(in.EVMaximumVarAbsorptionDuringDischarging, out.maximum_var_absorption_during_discharging);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumVarAbsorptionDuringDischarging_L2,
                           out.maximum_var_absorption_during_discharging_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumVarAbsorptionDuringDischarging_L3,
                           out.maximum_var_absorption_during_discharging_L3);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumVarAbsorptionDuringDischarging, out.minimum_var_absorption_during_discharging);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumVarAbsorptionDuringDischarging_L2,
                           out.minimum_var_absorption_during_discharging_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumVarAbsorptionDuringDischarging_L3,
                           out.minimum_var_absorption_during_discharging_L3);
    convert(in.EVMaximumVarInjectionDuringDischarging, out.maximum_var_injection_during_discharging);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumVarInjectionDuringDischarging_L2,
                           out.maximum_var_injection_during_discharging_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumVarInjectionDuringDischarging_L3,
                           out.maximum_var_injection_during_discharging_L3);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumVarInjectionDuringDischarging, out.minimum_var_injection_during_discharging);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumVarInjectionDuringDischarging_L2,
                           out.minimum_var_injection_during_discharging_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumVarInjectionDuringDischarging_L3,
                           out.minimum_var_injection_during_discharging_L3);
}

template <> void convert(const datatypes::EVReactivePower& in, struct iso20_ac_der_sae_EVReactivePowerType& out) {
    init_iso20_ac_der_sae_EVReactivePowerType(&out);
    convert(in.maximum_var_absorption_during_charging, out.EVMaximumVarAbsorptionDuringCharging);
    CPP2CB_CONVERT_IF_USED(in.maximum_var_absorption_during_charging_L2, out.EVMaximumVarAbsorptionDuringCharging_L2);
    CPP2CB_CONVERT_IF_USED(in.maximum_var_absorption_during_charging_L3, out.EVMaximumVarAbsorptionDuringCharging_L3);
    CPP2CB_CONVERT_IF_USED(in.minimum_var_absorption_during_charging, out.EVMinimumVarAbsorptionDuringCharging);
    CPP2CB_CONVERT_IF_USED(in.minimum_var_absorption_during_charging_L2, out.EVMinimumVarAbsorptionDuringCharging_L2);
    CPP2CB_CONVERT_IF_USED(in.minimum_var_absorption_during_charging_L3, out.EVMinimumVarAbsorptionDuringCharging_L3);
    convert(in.maximum_var_injection_during_charging, out.EVMaximumVarInjectionDuringCharging);
    CPP2CB_CONVERT_IF_USED(in.maximum_var_injection_during_charging_L2, out.EVMaximumVarInjectionDuringCharging_L2);
    CPP2CB_CONVERT_IF_USED(in.maximum_var_injection_during_charging_L3, out.EVMaximumVarInjectionDuringCharging_L3);
    CPP2CB_CONVERT_IF_USED(in.minimum_var_injection_during_charging, out.EVMinimumVarInjectionDuringCharging);
    CPP2CB_CONVERT_IF_USED(in.minimum_var_injection_during_charging_L2, out.EVMinimumVarInjectionDuringCharging_L2);
    CPP2CB_CONVERT_IF_USED(in.minimum_var_injection_during_charging_L3, out.EVMinimumVarInjectionDuringCharging_L3);
    convert(in.maximum_var_absorption_during_discharging, out.EVMaximumVarAbsorptionDuringDischarging);
    CPP2CB_CONVERT_IF_USED(in.maximum_var_absorption_during_discharging_L2,
                           out.EVMaximumVarAbsorptionDuringDischarging_L2);
    CPP2CB_CONVERT_IF_USED(in.maximum_var_absorption_during_discharging_L3,
                           out.EVMaximumVarAbsorptionDuringDischarging_L3);
    CPP2CB_CONVERT_IF_USED(in.minimum_var_absorption_during_discharging, out.EVMinimumVarAbsorptionDuringDischarging);
    CPP2CB_CONVERT_IF_USED(in.minimum_var_absorption_during_discharging_L2,
                           out.EVMinimumVarAbsorptionDuringDischarging_L2);
    CPP2CB_CONVERT_IF_USED(in.minimum_var_absorption_during_discharging_L3,
                           out.EVMinimumVarAbsorptionDuringDischarging_L3);
    convert(in.maximum_var_injection_during_discharging, out.EVMaximumVarInjectionDuringDischarging);
    CPP2CB_CONVERT_IF_USED(in.maximum_var_injection_during_discharging_L2,
                           out.EVMaximumVarInjectionDuringDischarging_L2);
    CPP2CB_CONVERT_IF_USED(in.maximum_var_injection_during_discharging_L3,
                           out.EVMaximumVarInjectionDuringDischarging_L3);
    CPP2CB_CONVERT_IF_USED(in.minimum_var_injection_during_discharging, out.EVMinimumVarInjectionDuringDischarging);
    CPP2CB_CONVERT_IF_USED(in.minimum_var_injection_during_discharging_L2,
                           out.EVMinimumVarInjectionDuringDischarging_L2);
    CPP2CB_CONVERT_IF_USED(in.minimum_var_injection_during_discharging_L3,
                           out.EVMinimumVarInjectionDuringDischarging_L3);
}

template <> void convert(const struct iso20_ac_der_sae_EVExcitationType& in, datatypes::EVExcitation& out) {
    CB2CPP_CONVERT_IF_USED(in.EVSpecifiedOverExcitedPowerFactor, out.specified_over_excited_power_factor);
    CB2CPP_CONVERT_IF_USED(in.EVSpecifiedOverExcitedPowerFactor_L2, out.specified_over_excited_power_factor_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSpecifiedOverExcitedPowerFactor_L3, out.specified_over_excited_power_factor_L3);
    convert(in.EVSpecifiedOverExcitedDischargePower, out.specified_over_excited_discharge_power);
    CB2CPP_CONVERT_IF_USED(in.EVSpecifiedOverExcitedDischargePower_L2, out.specified_over_excited_discharge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSpecifiedOverExcitedDischargePower_L3, out.specified_over_excited_discharge_power_L3);
    CB2CPP_CONVERT_IF_USED(in.EVSpecifiedUnderExcitedPowerFactor, out.specified_under_excited_power_factor);
    CB2CPP_CONVERT_IF_USED(in.EVSpecifiedUnderExcitedPowerFactor_L2, out.specified_under_excited_power_factor_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSpecifiedUnderExcitedPowerFactor_L3, out.specified_under_excited_power_factor_L3);
    convert(in.EVSpecifiedUnderExcitedDischargePower, out.specified_under_excited_discharge_power);
    CB2CPP_CONVERT_IF_USED(in.EVSpecifiedUnderExcitedDischargePower_L2, out.specified_under_excited_discharge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSpecifiedUnderExcitedDischargePower_L3, out.specified_under_excited_discharge_power_L3);
}

template <> void convert(const datatypes::EVExcitation& in, struct iso20_ac_der_sae_EVExcitationType& out) {
    init_iso20_ac_der_sae_EVExcitationType(&out);
    CPP2CB_CONVERT_IF_USED(in.specified_over_excited_power_factor, out.EVSpecifiedOverExcitedPowerFactor);
    CPP2CB_CONVERT_IF_USED(in.specified_over_excited_power_factor_L2, out.EVSpecifiedOverExcitedPowerFactor_L2);
    CPP2CB_CONVERT_IF_USED(in.specified_over_excited_power_factor_L3, out.EVSpecifiedOverExcitedPowerFactor_L3);
    convert(in.specified_over_excited_discharge_power, out.EVSpecifiedOverExcitedDischargePower);
    CPP2CB_CONVERT_IF_USED(in.specified_over_excited_discharge_power_L2, out.EVSpecifiedOverExcitedDischargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.specified_over_excited_discharge_power_L3, out.EVSpecifiedOverExcitedDischargePower_L3);
    CPP2CB_CONVERT_IF_USED(in.specified_under_excited_power_factor, out.EVSpecifiedUnderExcitedPowerFactor);
    CPP2CB_CONVERT_IF_USED(in.specified_under_excited_power_factor_L2, out.EVSpecifiedUnderExcitedPowerFactor_L2);
    CPP2CB_CONVERT_IF_USED(in.specified_under_excited_power_factor_L3, out.EVSpecifiedUnderExcitedPowerFactor_L3);
    convert(in.specified_under_excited_discharge_power, out.EVSpecifiedUnderExcitedDischargePower);
    CPP2CB_CONVERT_IF_USED(in.specified_under_excited_discharge_power_L2, out.EVSpecifiedUnderExcitedDischargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.specified_under_excited_discharge_power_L3, out.EVSpecifiedUnderExcitedDischargePower_L3);
}

template <>
void convert(const datatypes::DER_Dynamic_AC_CLReqControlMode& in,
             struct iso20_ac_der_sae_DER_Dynamic_AC_CLReqControlModeType& out) {
    init_iso20_ac_der_sae_DER_Dynamic_AC_CLReqControlModeType(&out);

    CPP2CB_ASSIGN_IF_USED(in.departure_time, out.DepartureTime);
    convert(in.target_energy_request, out.EVTargetEnergyRequest);
    convert(in.max_energy_request, out.EVMaximumEnergyRequest);
    convert(in.min_energy_request, out.EVMinimumEnergyRequest);

    convert(in.max_charge_power, out.EVMaximumChargePower);
    CPP2CB_CONVERT_IF_USED(in.max_charge_power_L2, out.EVMaximumChargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.max_charge_power_L3, out.EVMaximumChargePower_L3);
    convert(in.min_charge_power, out.EVMinimumChargePower);
    CPP2CB_CONVERT_IF_USED(in.min_charge_power_L2, out.EVMinimumChargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.min_charge_power_L3, out.EVMinimumChargePower_L3);
    convert(in.present_active_power, out.EVPresentActivePower);
    CPP2CB_CONVERT_IF_USED(in.present_active_power_L2, out.EVPresentActivePower_L2);
    CPP2CB_CONVERT_IF_USED(in.present_active_power_L3, out.EVPresentActivePower_L3);
    convert(in.present_reactive_power, out.EVPresentReactivePower);
    CPP2CB_CONVERT_IF_USED(in.present_reactive_power_L2, out.EVPresentReactivePower_L2);
    CPP2CB_CONVERT_IF_USED(in.present_reactive_power_L3, out.EVPresentReactivePower_L3);

    convert(in.maximum_discharge_power, out.EVMaximumDischargePower);
    CPP2CB_CONVERT_IF_USED(in.maximum_discharge_power_L2, out.EVMaximumDischargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.maximum_discharge_power_L3, out.EVMaximumDischargePower_L3);
    convert(in.minimum_discharge_power, out.EVMinimumDischargePower);
    CPP2CB_CONVERT_IF_USED(in.minimum_discharge_power_L2, out.EVMinimumDischargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.minimum_discharge_power_L3, out.EVMinimumDischargePower_L3);
    convert(in.present_voltage, out.EVPresentVoltage);
    convert(in.present_frequency, out.EVPresentFrequency);
    CPP2CB_CONVERT_IF_USED(in.session_total_discharge_energy_available, out.EVSessionTotalDischargeEnergyAvailable);
    CPP2CB_CONVERT_IF_USED(in.apparent_power, out.EVApparentPower);
    CPP2CB_CONVERT_IF_USED(in.reactive_power, out.EVReactivePower);
    CPP2CB_CONVERT_IF_USED(in.excitation, out.EVExcitation);
    CPP2CB_CONVERT_IF_USED(in.maximum_v2x_energy_request, out.EVMaximumV2XEnergyRequest);
    CPP2CB_CONVERT_IF_USED(in.minimum_v2x_energy_request, out.EVMinimumV2XEnergyRequest);

    cb_convert_enum(in.der_operational_state, out.DEROperationalState);
    cb_convert_enum(in.der_connection_status, out.DERConnectionStatus);

    out.EVUpdateTime = in.update_time;
    out.EVMinimumChargingDuration = in.minimum_charging_duration;
    out.EVDurationMaximumChargeRate = in.duration_maximum_charge_rate;
    out.EVDurationMaximumDischargeRate = in.duration_maximum_discharge_rate;
    out.DERAlarmStatus = in.der_alarm_status;
    out.EnabledModes = in.enabled_modes;
}

template <>
void convert(const struct iso20_ac_der_sae_DER_Dynamic_AC_CLReqControlModeType& in,
             datatypes::DER_Dynamic_AC_CLReqControlMode& out) {
    CB2CPP_ASSIGN_IF_USED(in.DepartureTime, out.departure_time);
    convert(in.EVTargetEnergyRequest, out.target_energy_request);
    convert(in.EVMaximumEnergyRequest, out.max_energy_request);
    convert(in.EVMinimumEnergyRequest, out.min_energy_request);

    convert(in.EVMaximumChargePower, out.max_charge_power);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumChargePower_L2, out.max_charge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumChargePower_L3, out.max_charge_power_L3);
    convert(in.EVMinimumChargePower, out.min_charge_power);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumChargePower_L2, out.min_charge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumChargePower_L3, out.min_charge_power_L3);
    convert(in.EVPresentActivePower, out.present_active_power);
    CB2CPP_CONVERT_IF_USED(in.EVPresentActivePower_L2, out.present_active_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVPresentActivePower_L3, out.present_active_power_L3);
    convert(in.EVPresentReactivePower, out.present_reactive_power);
    CB2CPP_CONVERT_IF_USED(in.EVPresentReactivePower_L2, out.present_reactive_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVPresentReactivePower_L3, out.present_reactive_power_L3);

    convert(in.EVMaximumDischargePower, out.maximum_discharge_power);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumDischargePower_L2, out.maximum_discharge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumDischargePower_L3, out.maximum_discharge_power_L3);
    convert(in.EVMinimumDischargePower, out.minimum_discharge_power);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumDischargePower_L2, out.minimum_discharge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumDischargePower_L3, out.minimum_discharge_power_L3);
    convert(in.EVPresentVoltage, out.present_voltage);
    convert(in.EVPresentFrequency, out.present_frequency);
    CB2CPP_CONVERT_IF_USED(in.EVSessionTotalDischargeEnergyAvailable, out.session_total_discharge_energy_available);
    CB2CPP_CONVERT_IF_USED(in.EVApparentPower, out.apparent_power);
    CB2CPP_CONVERT_IF_USED(in.EVReactivePower, out.reactive_power);
    CB2CPP_CONVERT_IF_USED(in.EVExcitation, out.excitation);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumV2XEnergyRequest, out.maximum_v2x_energy_request);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumV2XEnergyRequest, out.minimum_v2x_energy_request);

    cb_convert_enum(in.DEROperationalState, out.der_operational_state);
    cb_convert_enum(in.DERConnectionStatus, out.der_connection_status);

    out.update_time = in.EVUpdateTime;
    out.minimum_charging_duration = in.EVMinimumChargingDuration;
    out.duration_maximum_charge_rate = in.EVDurationMaximumChargeRate;
    out.duration_maximum_discharge_rate = in.EVDurationMaximumDischargeRate;
    out.der_alarm_status = in.DERAlarmStatus;
    out.enabled_modes = in.EnabledModes;
}

template <>
void convert(const datatypes::DER_Scheduled_AC_CLReqControlMode& in,
             struct iso20_ac_der_sae_DER_Scheduled_AC_CLReqControlModeType& out) {
    init_iso20_ac_der_sae_DER_Scheduled_AC_CLReqControlModeType(&out);

    CPP2CB_CONVERT_IF_USED(in.target_energy_request, out.EVTargetEnergyRequest);
    CPP2CB_CONVERT_IF_USED(in.max_energy_request, out.EVMaximumEnergyRequest);
    CPP2CB_CONVERT_IF_USED(in.min_energy_request, out.EVMinimumEnergyRequest);

    CPP2CB_CONVERT_IF_USED(in.max_charge_power, out.EVMaximumChargePower);
    CPP2CB_CONVERT_IF_USED(in.max_charge_power_L2, out.EVMaximumChargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.max_charge_power_L3, out.EVMaximumChargePower_L3);
    CPP2CB_CONVERT_IF_USED(in.min_charge_power, out.EVMinimumChargePower);
    CPP2CB_CONVERT_IF_USED(in.min_charge_power_L2, out.EVMinimumChargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.min_charge_power_L3, out.EVMinimumChargePower_L3);
    convert(in.present_active_power, out.EVPresentActivePower);
    CPP2CB_CONVERT_IF_USED(in.present_active_power_L2, out.EVPresentActivePower_L2);
    CPP2CB_CONVERT_IF_USED(in.present_active_power_L3, out.EVPresentActivePower_L3);
    CPP2CB_CONVERT_IF_USED(in.present_reactive_power, out.EVPresentReactivePower);
    CPP2CB_CONVERT_IF_USED(in.present_reactive_power_L2, out.EVPresentReactivePower_L2);
    CPP2CB_CONVERT_IF_USED(in.present_reactive_power_L3, out.EVPresentReactivePower_L3);

    convert(in.present_voltage, out.EVPresentVoltage);
    convert(in.present_frequency, out.EVPresentFrequency);

    CPP2CB_CONVERT_IF_USED(in.maximum_discharge_power, out.EVMaximumDischargePower);
    CPP2CB_CONVERT_IF_USED(in.maximum_discharge_power_L2, out.EVMaximumDischargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.maximum_discharge_power_L3, out.EVMaximumDischargePower_L3);
    CPP2CB_CONVERT_IF_USED(in.minimum_discharge_power, out.EVMinimumDischargePower);
    CPP2CB_CONVERT_IF_USED(in.minimum_discharge_power_L2, out.EVMinimumDischargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.minimum_discharge_power_L3, out.EVMinimumDischargePower_L3);

    cb_convert_enum(in.der_operational_state, out.DEROperationalState);
    cb_convert_enum(in.der_connection_status, out.DERConnectionStatus);

    CPP2CB_CONVERT_IF_USED(in.apparent_power, out.EVApparentPower);
    CPP2CB_CONVERT_IF_USED(in.reactive_power, out.EVReactivePower);
    CPP2CB_CONVERT_IF_USED(in.excitation, out.EVExcitation);

    out.EVUpdateTime = in.update_time;
    CPP2CB_ASSIGN_IF_USED(in.minimum_charging_duration, out.EVMinimumChargingDuration);
    CPP2CB_ASSIGN_IF_USED(in.duration_maximum_charge_rate, out.EVDurationMaximumChargeRate);
    CPP2CB_ASSIGN_IF_USED(in.duration_maximum_discharge_rate, out.EVDurationMaximumDischargeRate);
    out.DERAlarmStatus = in.der_alarm_status;
    out.EnabledModes = in.enabled_modes;
}

template <>
void convert(const struct iso20_ac_der_sae_DER_Scheduled_AC_CLReqControlModeType& in,
             datatypes::DER_Scheduled_AC_CLReqControlMode& out) {
    CB2CPP_CONVERT_IF_USED(in.EVTargetEnergyRequest, out.target_energy_request);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumEnergyRequest, out.max_energy_request);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumEnergyRequest, out.min_energy_request);

    CB2CPP_CONVERT_IF_USED(in.EVMaximumChargePower, out.max_charge_power);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumChargePower_L2, out.max_charge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumChargePower_L3, out.max_charge_power_L3);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumChargePower, out.min_charge_power);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumChargePower_L2, out.min_charge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumChargePower_L3, out.min_charge_power_L3);
    convert(in.EVPresentActivePower, out.present_active_power);
    CB2CPP_CONVERT_IF_USED(in.EVPresentActivePower_L2, out.present_active_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVPresentActivePower_L3, out.present_active_power_L3);
    CB2CPP_CONVERT_IF_USED(in.EVPresentReactivePower, out.present_reactive_power);
    CB2CPP_CONVERT_IF_USED(in.EVPresentReactivePower_L2, out.present_reactive_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVPresentReactivePower_L3, out.present_reactive_power_L3);

    convert(in.EVPresentVoltage, out.present_voltage);
    convert(in.EVPresentFrequency, out.present_frequency);

    CB2CPP_CONVERT_IF_USED(in.EVMaximumDischargePower, out.maximum_discharge_power);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumDischargePower_L2, out.maximum_discharge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumDischargePower_L3, out.maximum_discharge_power_L3);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumDischargePower, out.minimum_discharge_power);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumDischargePower_L2, out.minimum_discharge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumDischargePower_L3, out.minimum_discharge_power_L3);

    cb_convert_enum(in.DEROperationalState, out.der_operational_state);
    cb_convert_enum(in.DERConnectionStatus, out.der_connection_status);

    CB2CPP_CONVERT_IF_USED(in.EVApparentPower, out.apparent_power);
    CB2CPP_CONVERT_IF_USED(in.EVReactivePower, out.reactive_power);
    CB2CPP_CONVERT_IF_USED(in.EVExcitation, out.excitation);

    out.update_time = in.EVUpdateTime;
    CB2CPP_ASSIGN_IF_USED(in.EVMinimumChargingDuration, out.minimum_charging_duration);
    CB2CPP_ASSIGN_IF_USED(in.EVDurationMaximumChargeRate, out.duration_maximum_charge_rate);
    CB2CPP_ASSIGN_IF_USED(in.EVDurationMaximumDischargeRate, out.duration_maximum_discharge_rate);
    out.der_alarm_status = in.DERAlarmStatus;
    out.enabled_modes = in.EnabledModes;
}

struct ReqControlModeVisitor {
    using DER_ScheduledCM = datatypes::DER_Scheduled_AC_CLReqControlMode;
    using DER_DynamicCM = datatypes::DER_Dynamic_AC_CLReqControlMode;

    ReqControlModeVisitor(iso20_ac_der_sae_AC_ChargeLoopReqType& req_) : req(req_) {};

    void operator()(const DER_ScheduledCM& in) {
        auto& out = req.DER_Scheduled_AC_CLReqControlMode;
        init_iso20_ac_der_sae_DER_Scheduled_AC_CLReqControlModeType(&out);
        convert(in, out);
        CB_SET_USED(req.DER_Scheduled_AC_CLReqControlMode);
    }

    void operator()(const DER_DynamicCM& in) {
        auto& out = req.DER_Dynamic_AC_CLReqControlMode;
        init_iso20_ac_der_sae_DER_Dynamic_AC_CLReqControlModeType(&out);
        convert(in, out);
        CB_SET_USED(req.DER_Dynamic_AC_CLReqControlMode);
    }

private:
    iso20_ac_der_sae_AC_ChargeLoopReqType& req;
};

template <> void convert(const DER_SAE_AC_ChargeLoopRequest& in, struct iso20_ac_der_sae_AC_ChargeLoopReqType& out) {
    init_iso20_ac_der_sae_AC_ChargeLoopReqType(&out);

    convert(in.header, out.Header);

    CPP2CB_CONVERT_IF_USED(in.display_parameters, out.DisplayParameters);
    out.MeterInfoRequested = static_cast<int>(in.meter_info_requested);

    std::visit(ReqControlModeVisitor(out), in.control_mode);
}

template <> void convert(const struct iso20_ac_der_sae_AC_ChargeLoopReqType& in, DER_SAE_AC_ChargeLoopRequest& out) {
    convert(in.Header, out.header);

    CB2CPP_CONVERT_IF_USED(in.DisplayParameters, out.display_parameters);
    out.meter_info_requested = static_cast<bool>(in.MeterInfoRequested);

    if (in.DER_Scheduled_AC_CLReqControlMode_isUsed) {
        convert(in.DER_Scheduled_AC_CLReqControlMode,
                out.control_mode.emplace<datatypes::DER_Scheduled_AC_CLReqControlMode>());
    } else if (in.DER_Dynamic_AC_CLReqControlMode_isUsed) {
        convert(in.DER_Dynamic_AC_CLReqControlMode,
                out.control_mode.emplace<datatypes::DER_Dynamic_AC_CLReqControlMode>());
    } else {
        // should not happen
        assert(false);
    }
}

template <> int serialize_to_exi(const DER_SAE_AC_ChargeLoopRequest& in, exi_bitstream_t& out) {
    iso20_ac_der_sae_exiDocument doc{};
    init_iso20_ac_der_sae_exiDocument(&doc);

    CB_SET_USED(doc.AC_ChargeLoopReq);

    convert(in, doc.AC_ChargeLoopReq);

    return encode_iso20_ac_der_sae_exiDocument(&out, &doc);
}

template <> size_t serialize(const DER_SAE_AC_ChargeLoopRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> void insert_type(VariantAccess& va, const struct iso20_ac_der_sae_AC_ChargeLoopReqType& in) {
    va.insert_type<DER_SAE_AC_ChargeLoopRequest>(in);
}

// Response message

struct ResControlModeVisitor {
    using DER_ScheduledCM = datatypes::DER_Scheduled_AC_CLResControlMode;
    using DER_DynamicCM = datatypes::DER_Dynamic_AC_CLResControlMode;

    ResControlModeVisitor(iso20_ac_der_sae_AC_ChargeLoopResType& req_) : req(req_) {};

    void operator()(const DER_ScheduledCM& in) {
        auto& out = req.DER_Scheduled_AC_CLResControlMode;
        init_iso20_ac_der_sae_DER_Scheduled_AC_CLResControlModeType(&out);
        convert(in, out);
        CB_SET_USED(req.DER_Scheduled_AC_CLResControlMode);
    }

    void operator()(const DER_DynamicCM& in) {
        auto& out = req.DER_Dynamic_AC_CLResControlMode;
        init_iso20_ac_der_sae_DER_Dynamic_AC_CLResControlModeType(&out);
        convert(in, out);
        CB_SET_USED(req.DER_Dynamic_AC_CLResControlMode);
    }

private:
    iso20_ac_der_sae_AC_ChargeLoopResType& req;
};

template <> void convert(const DER_SAE_AC_ChargeLoopResponse& in, struct iso20_ac_der_sae_AC_ChargeLoopResType& out) {
    init_iso20_ac_der_sae_AC_ChargeLoopResType(&out);

    convert(in.header, out.Header);
    cb_convert_enum(in.response_code, out.ResponseCode);

    CPP2CB_CONVERT_IF_USED(in.status, out.EVSEStatus);
    CPP2CB_CONVERT_IF_USED(in.meter_info, out.MeterInfo);
    CPP2CB_CONVERT_IF_USED(in.receipt, out.Receipt);

    CPP2CB_CONVERT_IF_USED(in.target_frequency, out.EVSETargetFrequency);

    std::visit(ResControlModeVisitor(out), in.control_mode);
}

template <> void convert(const struct iso20_ac_der_sae_AC_ChargeLoopResType& in, DER_SAE_AC_ChargeLoopResponse& out) {
    convert(in.Header, out.header);
    cb_convert_enum(in.ResponseCode, out.response_code);
    CB2CPP_CONVERT_IF_USED(in.MeterInfo, out.meter_info);
    CB2CPP_CONVERT_IF_USED(in.Receipt, out.receipt);
    CB2CPP_CONVERT_IF_USED(in.EVSEStatus, out.status);
    CB2CPP_CONVERT_IF_USED(in.EVSETargetFrequency, out.target_frequency);

    if (in.DER_Scheduled_AC_CLResControlMode_isUsed) {
        convert(in.DER_Scheduled_AC_CLResControlMode,
                out.control_mode.emplace<datatypes::DER_Scheduled_AC_CLResControlMode>());
    } else if (in.DER_Dynamic_AC_CLResControlMode_isUsed) {
        convert(in.DER_Dynamic_AC_CLResControlMode,
                out.control_mode.emplace<datatypes::DER_Dynamic_AC_CLResControlMode>());
    } else {
        // should not happen
        assert(false);
    }
}

template <> int serialize_to_exi(const DER_SAE_AC_ChargeLoopResponse& in, exi_bitstream_t& out) {
    iso20_ac_der_sae_exiDocument doc{};
    init_iso20_ac_der_sae_exiDocument(&doc);

    CB_SET_USED(doc.AC_ChargeLoopRes);

    convert(in, doc.AC_ChargeLoopRes);

    return encode_iso20_ac_der_sae_exiDocument(&out, &doc);
}

template <> size_t serialize(const DER_SAE_AC_ChargeLoopResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> void insert_type(VariantAccess& va, const struct iso20_ac_der_sae_AC_ChargeLoopResType& in) {
    va.insert_type<DER_SAE_AC_ChargeLoopResponse>(in);
}

} // namespace iso15118::message_20
