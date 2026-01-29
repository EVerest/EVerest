// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/message/ac_der_sae_charge_loop.hpp>

#include <iso15118/detail/variant_access.hpp>

#include <cbv2g/iso_20/iso20_AC_DER_SAE_Decoder.h>
#include <cbv2g/iso_20/iso20_AC_DER_SAE_Encoder.h>

namespace iso15118::message_20 {

namespace dts = datatypes::sae;

// Request

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

template <> void convert(const struct iso20_ac_der_sae_EVApparentPowerType& in, dts::EVApparentPower& out) {
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

template <> void convert(const dts::EVApparentPower& in, struct iso20_ac_der_sae_EVApparentPowerType& out) {
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

template <> void convert(const struct iso20_ac_der_sae_EVReactivePowerType& in, dts::EVReactivePower& out) {
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

template <> void convert(const dts::EVReactivePower& in, struct iso20_ac_der_sae_EVReactivePowerType& out) {
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

template <> void convert(const struct iso20_ac_der_sae_EVExcitationType& in, dts::EVExcitation& out) {
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

template <> void convert(const dts::EVExcitation& in, struct iso20_ac_der_sae_EVExcitationType& out) {
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
void convert(const dts::DER_Scheduled_AC_CLReqControlMode& in,
             struct iso20_ac_der_sae_DER_Scheduled_AC_CLReqControlModeType& out) {
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

    if (in.apparent_power.has_value()) {
        convert(in.apparent_power.value(), out.EVApparentPower);
        CB_SET_USED(out.EVApparentPower);
    }
    if (in.reactive_power.has_value()) {
        convert(in.reactive_power.value(), out.EVReactivePower);
        CB_SET_USED(out.EVReactivePower);
    }
    if (in.excitation.has_value()) {
        convert(in.excitation.value(), out.EVExcitation);
        CB_SET_USED(out.EVExcitation);
    }

    out.EVUpdateTime = in.update_time;
    CPP2CB_ASSIGN_IF_USED(in.minimum_charging_duration, out.EVMinimumChargingDuration);
    CPP2CB_ASSIGN_IF_USED(in.duration_maximum_charge_rate, out.EVDurationMaximumChargeRate);
    CPP2CB_ASSIGN_IF_USED(in.duration_maximum_discharge_rate, out.EVDurationMaximumDischargeRate);
    out.DERAlarmStatus = in.der_alarm_status;
    out.EnabledModes = in.enabled_modes;
}

template <>
void convert(const struct iso20_ac_der_sae_DER_Scheduled_AC_CLReqControlModeType& in,
             dts::DER_Scheduled_AC_CLReqControlMode& out) {
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

    if (in.EVApparentPower_isUsed) {
        convert(in.EVApparentPower, out.apparent_power.emplace());
    }
    if (in.EVReactivePower_isUsed) {
        convert(in.EVReactivePower, out.reactive_power.emplace());
    }
    if (in.EVExcitation_isUsed) {
        convert(in.EVExcitation, out.excitation.emplace());
    }

    out.update_time = in.EVUpdateTime;
    CB2CPP_ASSIGN_IF_USED(in.EVMinimumChargingDuration, out.minimum_charging_duration);
    CB2CPP_ASSIGN_IF_USED(in.EVDurationMaximumChargeRate, out.duration_maximum_charge_rate);
    CB2CPP_ASSIGN_IF_USED(in.EVDurationMaximumDischargeRate, out.duration_maximum_discharge_rate);
    out.der_alarm_status = in.DERAlarmStatus;
    out.enabled_modes = in.EnabledModes;
}

template <>
void convert(const dts::DER_Dynamic_AC_CLReqControlMode& in,
             struct iso20_ac_der_sae_DER_Dynamic_AC_CLReqControlModeType& out) {
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

    if (in.apparent_power.has_value()) {
        convert(in.apparent_power.value(), out.EVApparentPower);
        CB_SET_USED(out.EVApparentPower);
    }
    if (in.reactive_power.has_value()) {
        convert(in.reactive_power.value(), out.EVReactivePower);
        CB_SET_USED(out.EVReactivePower);
    }
    if (in.excitation.has_value()) {
        convert(in.excitation.value(), out.EVExcitation);
        CB_SET_USED(out.EVExcitation);
    }

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
             dts::DER_Dynamic_AC_CLReqControlMode& out) {
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

    if (in.EVApparentPower_isUsed) {
        convert(in.EVApparentPower, out.apparent_power.emplace());
    }
    if (in.EVReactivePower_isUsed) {
        convert(in.EVReactivePower, out.reactive_power.emplace());
    }
    if (in.EVExcitation_isUsed) {
        convert(in.EVExcitation, out.excitation.emplace());
    }

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

namespace {

struct ReqControlModeVisitor {
    using DER_ScheduledCM = dts::DER_Scheduled_AC_CLReqControlMode;
    using DER_DynamicCM = dts::DER_Dynamic_AC_CLReqControlMode;

    ReqControlModeVisitor(iso20_ac_der_sae_AC_ChargeLoopReqType& req_) : req(req_){};

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

} // namespace

template <> void convert(const DER_SAE_AC_ChargeLoopRequest& in, struct iso20_ac_der_sae_AC_ChargeLoopReqType& out) {
    init_iso20_ac_der_sae_AC_ChargeLoopReqType(&out);

    convert(in.header, out.Header);

    CPP2CB_CONVERT_IF_USED(in.display_parameters, out.DisplayParameters);
    out.MeterInfoRequested = in.meter_info_requested;

    std::visit(ReqControlModeVisitor(out), in.control_mode);
}

template <> void convert(const struct iso20_ac_der_sae_AC_ChargeLoopReqType& in, DER_SAE_AC_ChargeLoopRequest& out) {
    convert(in.Header, out.header);

    CB2CPP_CONVERT_IF_USED(in.DisplayParameters, out.display_parameters);
    out.meter_info_requested = in.MeterInfoRequested;

    if (in.DER_Scheduled_AC_CLReqControlMode_isUsed) {
        convert(in.DER_Scheduled_AC_CLReqControlMode,
                out.control_mode.emplace<dts::DER_Scheduled_AC_CLReqControlMode>());
    } else if (in.DER_Dynamic_AC_CLReqControlMode_isUsed) {
        convert(in.DER_Dynamic_AC_CLReqControlMode, out.control_mode.emplace<dts::DER_Dynamic_AC_CLReqControlMode>());
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

// Response

template <> void convert(const datatypes::DetailedCost& in, struct iso20_ac_der_sae_DetailedCostType& out) {
    init_iso20_ac_der_sae_DetailedCostType(&out);
    convert(in.amount, out.Amount);
    convert(in.cost_per_unit, out.CostPerUnit);
}

template <> void convert(const struct iso20_ac_der_sae_DetailedCostType& in, datatypes::DetailedCost& out) {
    convert(in.Amount, out.amount);
    convert(in.CostPerUnit, out.cost_per_unit);
}

template <> void convert(const datatypes::DetailedTax& in, struct iso20_ac_der_sae_DetailedTaxType& out) {
    init_iso20_ac_der_sae_DetailedTaxType(&out);
    out.TaxRuleID = in.tax_rule_id;
    convert(in.amount, out.Amount);
}

template <> void convert(const struct iso20_ac_der_sae_DetailedTaxType& in, datatypes::DetailedTax& out) {
    out.tax_rule_id = in.TaxRuleID;
    convert(in.Amount, out.amount);
}

template <> void convert(const datatypes::Receipt& in, struct iso20_ac_der_sae_ReceiptType& out) {
    init_iso20_ac_der_sae_ReceiptType(&out);

    out.TimeAnchor = in.time_anchor;
    CPP2CB_CONVERT_IF_USED(in.energy_costs, out.EnergyCosts);
    CPP2CB_CONVERT_IF_USED(in.occupancy_costs, out.OccupancyCosts);
    CPP2CB_CONVERT_IF_USED(in.additional_service_costs, out.AdditionalServicesCosts);
    CPP2CB_CONVERT_IF_USED(in.overstay_costs, out.OverstayCosts);

    if ((sizeof(out.TaxCosts.array) / sizeof(out.TaxCosts.array[0])) < in.tax_costs.size()) {
        throw std::runtime_error("tax costs array is too large");
    }
    for (std::size_t i = 0; i < in.tax_costs.size(); ++i) {
        convert(in.tax_costs[i], out.TaxCosts.array[i]);
    }
    out.TaxCosts.arrayLen = in.tax_costs.size();
}

template <> void convert(const struct iso20_ac_der_sae_ReceiptType& in, datatypes::Receipt& out) {
    out.time_anchor = in.TimeAnchor;

    CB2CPP_CONVERT_IF_USED(in.EnergyCosts, out.energy_costs);
    CB2CPP_CONVERT_IF_USED(in.OccupancyCosts, out.occupancy_costs);
    CB2CPP_CONVERT_IF_USED(in.AdditionalServicesCosts, out.additional_service_costs);
    CB2CPP_CONVERT_IF_USED(in.OverstayCosts, out.overstay_costs);

    if (in.TaxCosts.arrayLen > 10) {
        throw std::runtime_error("tax costs array is too large");
    }
    out.tax_costs.clear();
    for (std::size_t i = 0; i < in.TaxCosts.arrayLen; ++i) {
        convert(in.TaxCosts.array[i], out.tax_costs.emplace_back());
    }
}

template <> void convert(const struct iso20_ac_der_sae_EnterServiceCLResType& in, dts::EnterServiceCLRes& out) {
    out.permit_service = in.PermitService;
    CB2CPP_CONVERT_IF_USED(in.EnterServiceVoltageHigh, out.enter_service_voltage_high);
    CB2CPP_CONVERT_IF_USED(in.EnterServiceVoltageLow, out.enter_service_voltage_low);
    CB2CPP_CONVERT_IF_USED(in.EnterServiceFrequencyHigh, out.enter_service_frequency_high);
    CB2CPP_CONVERT_IF_USED(in.EnterServiceFrequencyLow, out.enter_service_frequency_low);
    CB2CPP_CONVERT_IF_USED(in.EnterServiceDelay, out.enter_service_delay);
    CB2CPP_CONVERT_IF_USED(in.EnterServiceRandomizedDelay, out.enter_service_randomized_delay);
    CB2CPP_CONVERT_IF_USED(in.EnterServiceRampTime, out.enter_service_ramp_time);
}

template <> void convert(const dts::EnterServiceCLRes& in, struct iso20_ac_der_sae_EnterServiceCLResType& out) {
    init_iso20_ac_der_sae_EnterServiceCLResType(&out);
    out.PermitService = in.permit_service;
    CPP2CB_CONVERT_IF_USED(in.enter_service_voltage_high, out.EnterServiceVoltageHigh);
    CPP2CB_CONVERT_IF_USED(in.enter_service_voltage_low, out.EnterServiceVoltageLow);
    CPP2CB_CONVERT_IF_USED(in.enter_service_frequency_high, out.EnterServiceFrequencyHigh);
    CPP2CB_CONVERT_IF_USED(in.enter_service_frequency_low, out.EnterServiceFrequencyLow);
    CPP2CB_CONVERT_IF_USED(in.enter_service_delay, out.EnterServiceDelay);
    CPP2CB_CONVERT_IF_USED(in.enter_service_randomized_delay, out.EnterServiceRandomizedDelay);
    CPP2CB_CONVERT_IF_USED(in.enter_service_ramp_time, out.EnterServiceRampTime);
}

template <>
void convert(const struct iso20_ac_der_sae_ReactivePowerSupportCLResType& in, dts::ReactivePowerSupportCLRes& out) {
    CB2CPP_CONVERT_IF_USED(in.ConstantPowerFactor, out.constant_power_factor);
    CB2CPP_CONVERT_IF_USED(in.VoltVar, out.volt_var);
    CB2CPP_CONVERT_IF_USED(in.WattVar, out.watt_var);
    CB2CPP_CONVERT_IF_USED(in.ConstantVar, out.constant_var);
}

template <>
void convert(const dts::ReactivePowerSupportCLRes& in, struct iso20_ac_der_sae_ReactivePowerSupportCLResType& out) {
    init_iso20_ac_der_sae_ReactivePowerSupportCLResType(&out);
    CPP2CB_CONVERT_IF_USED(in.constant_power_factor, out.ConstantPowerFactor);
    CPP2CB_CONVERT_IF_USED(in.volt_var, out.VoltVar);
    CPP2CB_CONVERT_IF_USED(in.watt_var, out.WattVar);
    CPP2CB_CONVERT_IF_USED(in.constant_var, out.ConstantVar);
}

template <>
void convert(const struct iso20_ac_der_sae_ActivePowerSupportCLResType& in, dts::ActivePowerSupportCLRes& out) {
    CB2CPP_CONVERT_IF_USED(in.FrequencyDroop, out.frequency_droop);
    CB2CPP_CONVERT_IF_USED(in.VoltWatt, out.volt_watt);
    CB2CPP_CONVERT_IF_USED(in.ConstantWatt, out.constant_watt);
    CB2CPP_CONVERT_IF_USED(in.LimitMaxDischargePower, out.limit_max_discharge_power);
}

template <>
void convert(const dts::ActivePowerSupportCLRes& in, struct iso20_ac_der_sae_ActivePowerSupportCLResType& out) {
    init_iso20_ac_der_sae_ActivePowerSupportCLResType(&out);
    CPP2CB_CONVERT_IF_USED(in.frequency_droop, out.FrequencyDroop);
    CPP2CB_CONVERT_IF_USED(in.volt_watt, out.VoltWatt);
    CPP2CB_CONVERT_IF_USED(in.constant_watt, out.ConstantWatt);
    CPP2CB_CONVERT_IF_USED(in.limit_max_discharge_power, out.LimitMaxDischargePower);
}

template <> void convert(const struct iso20_ac_der_sae_DERControlCLResType& in, dts::DERControlCLRes& out) {
    CB2CPP_CONVERT_IF_USED(in.VoltageTrip, out.voltage_trip);
    CB2CPP_CONVERT_IF_USED(in.FrequencyTrip, out.frequency_trip);
    convert(in.EnterServiceCLRes, out.enter_service_cl_res);
    CB2CPP_CONVERT_IF_USED(in.ReactivePowerSupportCLRes, out.reactive_power_support_cl_res);
    CB2CPP_CONVERT_IF_USED(in.ActivePowerSupportCLRes, out.active_power_support_cl_res);
}

template <> void convert(const dts::DERControlCLRes& in, struct iso20_ac_der_sae_DERControlCLResType& out) {
    init_iso20_ac_der_sae_DERControlCLResType(&out);
    CPP2CB_CONVERT_IF_USED(in.voltage_trip, out.VoltageTrip);
    CPP2CB_CONVERT_IF_USED(in.frequency_trip, out.FrequencyTrip);
    convert(in.enter_service_cl_res, out.EnterServiceCLRes);
    CPP2CB_CONVERT_IF_USED(in.reactive_power_support_cl_res, out.ReactivePowerSupportCLRes);
    CPP2CB_CONVERT_IF_USED(in.active_power_support_cl_res, out.ActivePowerSupportCLRes);
}

template <>
void convert(const struct iso20_ac_der_sae_DER_Scheduled_AC_CLResControlModeType& in,
             dts::DER_Scheduled_AC_CLResControlMode& out) {
    CB2CPP_CONVERT_IF_USED(in.EVSETargetActivePower, out.target_active_power);
    CB2CPP_CONVERT_IF_USED(in.EVSETargetActivePower_L2, out.target_active_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSETargetActivePower_L3, out.target_active_power_L3);

    CB2CPP_CONVERT_IF_USED(in.EVSETargetReactivePower, out.target_reactive_power);
    CB2CPP_CONVERT_IF_USED(in.EVSETargetReactivePower_L2, out.target_reactive_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSETargetReactivePower_L3, out.target_reactive_power_L3);

    CB2CPP_CONVERT_IF_USED(in.EVSEPresentActivePower, out.present_active_power);
    CB2CPP_CONVERT_IF_USED(in.EVSEPresentActivePower_L2, out.present_active_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSEPresentActivePower_L3, out.present_active_power_L3);

    convert(in.DERControlCLRes, out.der_control_cl_res);

    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumChargePower, out.evse_maximum_charge_power);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumChargePower_L2, out.evse_maximum_charge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumChargePower_L3, out.evse_maximum_charge_power_L3);

    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumDischargePower, out.evse_maximum_discharge_power);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumDischargePower_L2, out.evse_maximum_discharge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumDischargePower_L3, out.evse_maximum_discharge_power_L3);

    if (in.RequiredDEROperatingMode_isUsed) {
        cb_convert_enum(in.RequiredDEROperatingMode, out.required_der_operating_mode.emplace());
    }
    if (in.GridConnectionMode_isUsed) {
        cb_convert_enum(in.GridConnectionMode, out.grid_connection_mode.emplace());
    }
}

template <>
void convert(const dts::DER_Scheduled_AC_CLResControlMode& in,
             struct iso20_ac_der_sae_DER_Scheduled_AC_CLResControlModeType& out) {
    init_iso20_ac_der_sae_DER_Scheduled_AC_CLResControlModeType(&out);

    CPP2CB_CONVERT_IF_USED(in.target_active_power, out.EVSETargetActivePower);
    CPP2CB_CONVERT_IF_USED(in.target_active_power_L2, out.EVSETargetActivePower_L2);
    CPP2CB_CONVERT_IF_USED(in.target_active_power_L3, out.EVSETargetActivePower_L3);

    CPP2CB_CONVERT_IF_USED(in.target_reactive_power, out.EVSETargetReactivePower);
    CPP2CB_CONVERT_IF_USED(in.target_reactive_power_L2, out.EVSETargetReactivePower_L2);
    CPP2CB_CONVERT_IF_USED(in.target_reactive_power_L3, out.EVSETargetReactivePower_L3);

    CPP2CB_CONVERT_IF_USED(in.present_active_power, out.EVSEPresentActivePower);
    CPP2CB_CONVERT_IF_USED(in.present_active_power_L2, out.EVSEPresentActivePower_L2);
    CPP2CB_CONVERT_IF_USED(in.present_active_power_L3, out.EVSEPresentActivePower_L3);

    convert(in.der_control_cl_res, out.DERControlCLRes);

    CPP2CB_CONVERT_IF_USED(in.evse_maximum_charge_power, out.EVSEMaximumChargePower);
    CPP2CB_CONVERT_IF_USED(in.evse_maximum_charge_power_L2, out.EVSEMaximumChargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.evse_maximum_charge_power_L3, out.EVSEMaximumChargePower_L3);

    CPP2CB_CONVERT_IF_USED(in.evse_maximum_discharge_power, out.EVSEMaximumDischargePower);
    CPP2CB_CONVERT_IF_USED(in.evse_maximum_discharge_power_L2, out.EVSEMaximumDischargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.evse_maximum_discharge_power_L3, out.EVSEMaximumDischargePower_L3);

    if (in.required_der_operating_mode.has_value()) {
        cb_convert_enum(in.required_der_operating_mode.value(), out.RequiredDEROperatingMode);
        CB_SET_USED(out.RequiredDEROperatingMode);
    }
    if (in.grid_connection_mode.has_value()) {
        cb_convert_enum(in.grid_connection_mode.value(), out.GridConnectionMode);
        CB_SET_USED(out.GridConnectionMode);
    }
}

template <>
void convert(const struct iso20_ac_der_sae_DER_Dynamic_AC_CLResControlModeType& in,
             dts::DER_Dynamic_AC_CLResControlMode& out) {
    CB2CPP_ASSIGN_IF_USED(in.DepartureTime, out.departure_time);
    CB2CPP_ASSIGN_IF_USED(in.MinimumSOC, out.minimum_soc);
    CB2CPP_ASSIGN_IF_USED(in.TargetSOC, out.target_soc);
    CB2CPP_ASSIGN_IF_USED(in.AckMaxDelay, out.ack_max_delay);

    convert(in.EVSETargetActivePower, out.target_active_power);
    CB2CPP_CONVERT_IF_USED(in.EVSETargetActivePower_L2, out.target_active_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSETargetActivePower_L3, out.target_active_power_L3);

    CB2CPP_CONVERT_IF_USED(in.EVSETargetReactivePower, out.target_reactive_power);
    CB2CPP_CONVERT_IF_USED(in.EVSETargetReactivePower_L2, out.target_reactive_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSETargetReactivePower_L3, out.target_reactive_power_L3);

    CB2CPP_CONVERT_IF_USED(in.EVSEPresentActivePower, out.present_active_power);
    CB2CPP_CONVERT_IF_USED(in.EVSEPresentActivePower_L2, out.present_active_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSEPresentActivePower_L3, out.present_active_power_L3);

    convert(in.DERControlCLRes, out.der_control_cl_res);

    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumChargePower, out.evse_maximum_charge_power);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumChargePower_L2, out.evse_maximum_charge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumChargePower_L3, out.evse_maximum_charge_power_L3);

    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumDischargePower, out.evse_maximum_discharge_power);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumDischargePower_L2, out.evse_maximum_discharge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumDischargePower_L3, out.evse_maximum_discharge_power_L3);

    if (in.RequiredDEROperatingMode_isUsed) {
        cb_convert_enum(in.RequiredDEROperatingMode, out.required_der_operating_mode.emplace());
    }
    if (in.GridConnectionMode_isUsed) {
        cb_convert_enum(in.GridConnectionMode, out.grid_connection_mode.emplace());
    }
}

template <>
void convert(const dts::DER_Dynamic_AC_CLResControlMode& in,
             struct iso20_ac_der_sae_DER_Dynamic_AC_CLResControlModeType& out) {
    init_iso20_ac_der_sae_DER_Dynamic_AC_CLResControlModeType(&out);

    CPP2CB_ASSIGN_IF_USED(in.departure_time, out.DepartureTime);
    CPP2CB_ASSIGN_IF_USED(in.minimum_soc, out.MinimumSOC);
    CPP2CB_ASSIGN_IF_USED(in.target_soc, out.TargetSOC);
    CPP2CB_ASSIGN_IF_USED(in.ack_max_delay, out.AckMaxDelay);

    convert(in.target_active_power, out.EVSETargetActivePower);
    CPP2CB_CONVERT_IF_USED(in.target_active_power_L2, out.EVSETargetActivePower_L2);
    CPP2CB_CONVERT_IF_USED(in.target_active_power_L3, out.EVSETargetActivePower_L3);

    CPP2CB_CONVERT_IF_USED(in.target_reactive_power, out.EVSETargetReactivePower);
    CPP2CB_CONVERT_IF_USED(in.target_reactive_power_L2, out.EVSETargetReactivePower_L2);
    CPP2CB_CONVERT_IF_USED(in.target_reactive_power_L3, out.EVSETargetReactivePower_L3);

    CPP2CB_CONVERT_IF_USED(in.present_active_power, out.EVSEPresentActivePower);
    CPP2CB_CONVERT_IF_USED(in.present_active_power_L2, out.EVSEPresentActivePower_L2);
    CPP2CB_CONVERT_IF_USED(in.present_active_power_L3, out.EVSEPresentActivePower_L3);

    convert(in.der_control_cl_res, out.DERControlCLRes);

    CPP2CB_CONVERT_IF_USED(in.evse_maximum_charge_power, out.EVSEMaximumChargePower);
    CPP2CB_CONVERT_IF_USED(in.evse_maximum_charge_power_L2, out.EVSEMaximumChargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.evse_maximum_charge_power_L3, out.EVSEMaximumChargePower_L3);

    CPP2CB_CONVERT_IF_USED(in.evse_maximum_discharge_power, out.EVSEMaximumDischargePower);
    CPP2CB_CONVERT_IF_USED(in.evse_maximum_discharge_power_L2, out.EVSEMaximumDischargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.evse_maximum_discharge_power_L3, out.EVSEMaximumDischargePower_L3);

    if (in.required_der_operating_mode.has_value()) {
        cb_convert_enum(in.required_der_operating_mode.value(), out.RequiredDEROperatingMode);
        CB_SET_USED(out.RequiredDEROperatingMode);
    }
    if (in.grid_connection_mode.has_value()) {
        cb_convert_enum(in.grid_connection_mode.value(), out.GridConnectionMode);
        CB_SET_USED(out.GridConnectionMode);
    }
}

namespace {

struct ControlModeVisitor {
    using DER_ScheduledCM = dts::DER_Scheduled_AC_CLResControlMode;
    using DER_DynamicCM = dts::DER_Dynamic_AC_CLResControlMode;

    ControlModeVisitor(iso20_ac_der_sae_AC_ChargeLoopResType& res_) : res(res_){};

    void operator()(const DER_ScheduledCM& in) {
        auto& out = res.DER_Scheduled_AC_CLResControlMode;
        init_iso20_ac_der_sae_DER_Scheduled_AC_CLResControlModeType(&out);
        convert(in, out);
        CB_SET_USED(res.DER_Scheduled_AC_CLResControlMode);
    }

    void operator()(const DER_DynamicCM& in) {
        auto& out = res.DER_Dynamic_AC_CLResControlMode;
        init_iso20_ac_der_sae_DER_Dynamic_AC_CLResControlModeType(&out);
        convert(in, out);
        CB_SET_USED(res.DER_Dynamic_AC_CLResControlMode);
    }

private:
    iso20_ac_der_sae_AC_ChargeLoopResType& res;
};

} // namespace

template <> void convert(const DER_SAE_AC_ChargeLoopResponse& in, struct iso20_ac_der_sae_AC_ChargeLoopResType& out) {
    init_iso20_ac_der_sae_AC_ChargeLoopResType(&out);

    convert(in.header, out.Header);
    cb_convert_enum(in.response_code, out.ResponseCode);

    CPP2CB_CONVERT_IF_USED(in.status, out.EVSEStatus);
    CPP2CB_CONVERT_IF_USED(in.meter_info, out.MeterInfo);
    CPP2CB_CONVERT_IF_USED(in.receipt, out.Receipt);

    CPP2CB_CONVERT_IF_USED(in.target_frequency, out.EVSETargetFrequency);

    std::visit(ControlModeVisitor(out), in.control_mode);
}

template <> void convert(const struct iso20_ac_der_sae_AC_ChargeLoopResType& in, DER_SAE_AC_ChargeLoopResponse& out) {
    convert(in.Header, out.header);
    cb_convert_enum(in.ResponseCode, out.response_code);
    CB2CPP_CONVERT_IF_USED(in.EVSEStatus, out.status);
    CB2CPP_CONVERT_IF_USED(in.MeterInfo, out.meter_info);
    CB2CPP_CONVERT_IF_USED(in.Receipt, out.receipt);
    CB2CPP_CONVERT_IF_USED(in.EVSETargetFrequency, out.target_frequency);

    if (in.DER_Scheduled_AC_CLResControlMode_isUsed) {
        convert(in.DER_Scheduled_AC_CLResControlMode,
                out.control_mode.emplace<dts::DER_Scheduled_AC_CLResControlMode>());
    } else if (in.DER_Dynamic_AC_CLResControlMode_isUsed) {
        convert(in.DER_Dynamic_AC_CLResControlMode, out.control_mode.emplace<dts::DER_Dynamic_AC_CLResControlMode>());
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
