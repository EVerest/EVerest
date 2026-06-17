// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/message/ac_der_sae_charge_parameter_discovery.hpp>

#include <iso15118/detail/variant_access.hpp>

#include <cbv2g/iso_20/iso20_AC_DER_SAE_Decoder.h>
#include <cbv2g/iso_20/iso20_AC_DER_SAE_Encoder.h>

namespace iso15118::message_20 {

using DER_AC_ModeReq = datatypes::sae::DER_SAE_AC_CPDReqEnergyTransferMode;
using DER_AC_ModeRes = datatypes::sae::DER_SAE_AC_CPDResEnergyTransferMode;

template <>
void convert(const struct iso20_ac_der_sae_EVApparentPowerLimitsType& in, datatypes::sae::EVApparentPowerLimits& out) {
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

template <>
void convert(const datatypes::sae::EVApparentPowerLimits& in, struct iso20_ac_der_sae_EVApparentPowerLimitsType& out) {
    init_iso20_ac_der_sae_EVApparentPowerLimitsType(&out);
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

template <>
void convert(const struct iso20_ac_der_sae_EVReactivePowerLimitsType& in, datatypes::sae::EVReactivePowerLimits& out) {
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
    convert(in.EVReactiveSusceptance, out.reactive_susceptance);
    CB2CPP_CONVERT_IF_USED(in.EVReactiveSusceptance_L2, out.reactive_susceptance_L2);
    CB2CPP_CONVERT_IF_USED(in.EVReactiveSusceptance_L3, out.reactive_susceptance_L3);
}

template <>
void convert(const datatypes::sae::EVReactivePowerLimits& in, struct iso20_ac_der_sae_EVReactivePowerLimitsType& out) {
    init_iso20_ac_der_sae_EVReactivePowerLimitsType(&out);
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
    convert(in.reactive_susceptance, out.EVReactiveSusceptance);
    CPP2CB_CONVERT_IF_USED(in.reactive_susceptance_L2, out.EVReactiveSusceptance_L2);
    CPP2CB_CONVERT_IF_USED(in.reactive_susceptance_L3, out.EVReactiveSusceptance_L3);
}

template <>
void convert(const struct iso20_ac_der_sae_EVExcitationLimitsType& in, datatypes::sae::EVExcitationLimits& out) {
    convert(in.EVSpecifiedOverExcitedPowerFactor, out.specified_over_excited_power_factor);
    CB2CPP_CONVERT_IF_USED(in.EVSpecifiedOverExcitedPowerFactor_L2, out.specified_over_excited_power_factor_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSpecifiedOverExcitedPowerFactor_L3, out.specified_over_excited_power_factor_L3);
    convert(in.EVSpecifiedOverExcitedDischargePower, out.specified_over_excited_discharge_power);
    CB2CPP_CONVERT_IF_USED(in.EVSpecifiedOverExcitedDischargePower_L2, out.specified_over_excited_discharge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSpecifiedOverExcitedDischargePower_L3, out.specified_over_excited_discharge_power_L3);
    convert(in.EVSpecifiedUnderExcitedPowerFactor, out.specified_under_excited_power_factor);
    CB2CPP_CONVERT_IF_USED(in.EVSpecifiedUnderExcitedPowerFactor_L2, out.specified_under_excited_power_factor_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSpecifiedUnderExcitedPowerFactor_L3, out.specified_under_excited_power_factor_L3);
    convert(in.EVSpecifiedUnderExcitedDischargePower, out.specified_under_excited_discharge_power);
    CB2CPP_CONVERT_IF_USED(in.EVSpecifiedUnderExcitedDischargePower_L2, out.specified_under_excited_discharge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSpecifiedUnderExcitedDischargePower_L3, out.specified_under_excited_discharge_power_L3);
}

template <>
void convert(const datatypes::sae::EVExcitationLimits& in, struct iso20_ac_der_sae_EVExcitationLimitsType& out) {
    init_iso20_ac_der_sae_EVExcitationLimitsType(&out);
    convert(in.specified_over_excited_power_factor, out.EVSpecifiedOverExcitedPowerFactor);
    CPP2CB_CONVERT_IF_USED(in.specified_over_excited_power_factor_L2, out.EVSpecifiedOverExcitedPowerFactor_L2);
    CPP2CB_CONVERT_IF_USED(in.specified_over_excited_power_factor_L3, out.EVSpecifiedOverExcitedPowerFactor_L3);
    convert(in.specified_over_excited_discharge_power, out.EVSpecifiedOverExcitedDischargePower);
    CPP2CB_CONVERT_IF_USED(in.specified_over_excited_discharge_power_L2, out.EVSpecifiedOverExcitedDischargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.specified_over_excited_discharge_power_L3, out.EVSpecifiedOverExcitedDischargePower_L3);
    convert(in.specified_under_excited_power_factor, out.EVSpecifiedUnderExcitedPowerFactor);
    CPP2CB_CONVERT_IF_USED(in.specified_under_excited_power_factor_L2, out.EVSpecifiedUnderExcitedPowerFactor_L2);
    CPP2CB_CONVERT_IF_USED(in.specified_under_excited_power_factor_L3, out.EVSpecifiedUnderExcitedPowerFactor_L3);
    convert(in.specified_under_excited_discharge_power, out.EVSpecifiedUnderExcitedDischargePower);
    CPP2CB_CONVERT_IF_USED(in.specified_under_excited_discharge_power_L2, out.EVSpecifiedUnderExcitedDischargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.specified_under_excited_discharge_power_L3, out.EVSpecifiedUnderExcitedDischargePower_L3);
}

template <>
void convert(const struct iso20_ac_der_sae_EVInverterDetailsType& in, datatypes::sae::EVInverterDetails& out) {
    out.inverter_sw_version = CB2CPP_STRING(in.EVInverterSwVersion);
    CB2CPP_STRING_IF_USED(in.EVInverterHwVersion, out.inverter_hw_version);
    out.inverter_manufacturer = CB2CPP_STRING(in.EVInverterManufacturer);
    out.inverter_model = CB2CPP_STRING(in.EVInverterModel);
    out.inverter_serial_number = CB2CPP_STRING(in.EVInverterSerialNumber);
}

template <>
void convert(const datatypes::sae::EVInverterDetails& in, struct iso20_ac_der_sae_EVInverterDetailsType& out) {
    init_iso20_ac_der_sae_EVInverterDetailsType(&out);
    CPP2CB_STRING(in.inverter_sw_version, out.EVInverterSwVersion);
    CPP2CB_STRING_IF_USED(in.inverter_hw_version, out.EVInverterHwVersion);
    CPP2CB_STRING(in.inverter_manufacturer, out.EVInverterManufacturer);
    CPP2CB_STRING(in.inverter_model, out.EVInverterModel);
    CPP2CB_STRING(in.inverter_serial_number, out.EVInverterSerialNumber);
}

template <> void convert(const struct iso20_ac_der_sae_DER_AC_CPDReqEnergyTransferModeType& in, DER_AC_ModeReq& out) {
    // Convert base AC_CPDReqEnergyTransferMode fields
    convert(in.EVMaximumChargePower, out.max_charge_power);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumChargePower_L2, out.max_charge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumChargePower_L3, out.max_charge_power_L3);
    convert(in.EVMinimumChargePower, out.min_charge_power);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumChargePower_L2, out.min_charge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumChargePower_L3, out.min_charge_power_L3);

    // Convert DER-specific fields
    cb_convert_enum(in.EVProcessing, out.processing);
    convert(in.EVMaximumDischargePower, out.maximum_discharge_power);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumDischargePower_L2, out.maximum_discharge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumDischargePower_L3, out.maximum_discharge_power_L3);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumDischargePower, out.minimum_discharge_power);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumDischargePower_L2, out.minimum_discharge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumDischargePower_L3, out.minimum_discharge_power_L3);
    CB2CPP_CONVERT_IF_USED(in.EVSessionTotalDischargeEnergyAvailable, out.session_total_discharge_energy_available);
    convert(in.EVApparentPowerLimits, out.apparent_power_limits);
    convert(in.EVReactivePowerLimits, out.reactive_power_limits);
    convert(in.EVExcitationLimits, out.excitation_limits);
    convert(in.EVInverterDetails, out.inverter_details);
    cb_convert_enum(in.IEEE1547NormalCategory, out.ieee1547_normal_category);
    cb_convert_enum(in.IEEE1547AbnormalCategory, out.ieee1547_abnormal_category);
    convert(in.EVNominalVoltage, out.nominal_voltage);
    convert(in.EVMaximumVoltage, out.maximum_voltage);
    convert(in.EVMinimumVoltage, out.minimum_voltage);
    convert(in.EVNominalVoltageOffset, out.nominal_voltage_offset);
    out.j3072_certified = static_cast<bool>(in.J3072Certified);
    out.j3072_certification_date = in.J3072CertificationDate;
    out.useable_watt_hours = in.EVUseableWattHours;
    out.update_time = in.EVUpdateTime;
    out.supported_modes = in.SupportedModes;
    out.enabled_modes = in.EnabledModes;
}

template <>
void convert(const struct iso20_ac_der_sae_AC_ChargeParameterDiscoveryReqType& in,
             DER_SAE_AC_ChargeParameterDiscoveryRequest& out) {
    convert(in.Header, out.header);

    if (in.DER_AC_CPDReqEnergyTransferMode_isUsed) {
        convert(in.DER_AC_CPDReqEnergyTransferMode, out.transfer_mode);
    } else {
        // TODO(SL): Add warning that a transfer mode other then DER
    }
}

template <> void insert_type(VariantAccess& va, const struct iso20_ac_der_sae_AC_ChargeParameterDiscoveryReqType& in) {
    va.insert_type<DER_SAE_AC_ChargeParameterDiscoveryRequest>(in);
}

template <>
void convert(const datatypes::sae::DER_SAE_AC_CPDReqEnergyTransferMode& in,
             struct iso20_ac_der_sae_DER_AC_CPDReqEnergyTransferModeType& out) {
    init_iso20_ac_der_sae_DER_AC_CPDReqEnergyTransferModeType(&out);

    // Convert base AC_CPDReqEnergyTransferMode fields
    convert(in.max_charge_power, out.EVMaximumChargePower);
    CPP2CB_CONVERT_IF_USED(in.max_charge_power_L2, out.EVMaximumChargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.max_charge_power_L3, out.EVMaximumChargePower_L3);
    convert(in.min_charge_power, out.EVMinimumChargePower);
    CPP2CB_CONVERT_IF_USED(in.min_charge_power_L2, out.EVMinimumChargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.min_charge_power_L3, out.EVMinimumChargePower_L3);

    // Convert DER-specific fields
    cb_convert_enum(in.processing, out.EVProcessing);
    convert(in.maximum_discharge_power, out.EVMaximumDischargePower);
    CPP2CB_CONVERT_IF_USED(in.maximum_discharge_power_L2, out.EVMaximumDischargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.maximum_discharge_power_L3, out.EVMaximumDischargePower_L3);
    CPP2CB_CONVERT_IF_USED(in.minimum_discharge_power, out.EVMinimumDischargePower);
    CPP2CB_CONVERT_IF_USED(in.minimum_discharge_power_L2, out.EVMinimumDischargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.minimum_discharge_power_L3, out.EVMinimumDischargePower_L3);
    CPP2CB_CONVERT_IF_USED(in.session_total_discharge_energy_available, out.EVSessionTotalDischargeEnergyAvailable);
    convert(in.apparent_power_limits, out.EVApparentPowerLimits);
    convert(in.reactive_power_limits, out.EVReactivePowerLimits);
    convert(in.excitation_limits, out.EVExcitationLimits);
    convert(in.inverter_details, out.EVInverterDetails);
    cb_convert_enum(in.ieee1547_normal_category, out.IEEE1547NormalCategory);
    cb_convert_enum(in.ieee1547_abnormal_category, out.IEEE1547AbnormalCategory);
    convert(in.nominal_voltage, out.EVNominalVoltage);
    convert(in.maximum_voltage, out.EVMaximumVoltage);
    convert(in.minimum_voltage, out.EVMinimumVoltage);
    convert(in.nominal_voltage_offset, out.EVNominalVoltageOffset);
    out.J3072Certified = in.j3072_certified ? 1 : 0;
    out.J3072CertificationDate = in.j3072_certification_date;
    out.EVUseableWattHours = in.useable_watt_hours;
    out.EVUpdateTime = in.update_time;
    out.SupportedModes = in.supported_modes;
    out.EnabledModes = in.enabled_modes;
}

template <>
void convert(const DER_SAE_AC_ChargeParameterDiscoveryRequest& in,
             struct iso20_ac_der_sae_AC_ChargeParameterDiscoveryReqType& out) {
    init_iso20_ac_der_sae_AC_ChargeParameterDiscoveryReqType(&out);
    convert(in.header, out.Header);

    convert(in.transfer_mode, out.DER_AC_CPDReqEnergyTransferMode);
    CB_SET_USED(out.DER_AC_CPDReqEnergyTransferMode);
}

template <> int serialize_to_exi(const DER_SAE_AC_ChargeParameterDiscoveryRequest& in, exi_bitstream_t& out) {
    iso20_ac_der_sae_exiDocument doc{};
    init_iso20_ac_der_sae_exiDocument(&doc);

    CB_SET_USED(doc.AC_ChargeParameterDiscoveryReq);
    convert(in, doc.AC_ChargeParameterDiscoveryReq);
    return encode_iso20_ac_der_sae_exiDocument(&out, &doc);
}
template <> size_t serialize(const DER_SAE_AC_ChargeParameterDiscoveryRequest& in, const io::StreamOutputView& out) {
    auto rv = serialize_helper(in, out);
    return rv;
}

template <>
void convert(const struct iso20_ac_der_sae_ActivePowerSupportCPDResType& in,
             datatypes::sae::ActivePowerSupportCPDRes& out) {
    convert(in.FrequencyDroop, out.frequency_droop);
    convert(in.VoltWatt, out.volt_watt);
    convert(in.ConstantWatt, out.constant_watt);
    convert(in.LimitMaxDischargePower, out.limit_max_discharge_power);
}

template <>
void convert(const datatypes::sae::ActivePowerSupportCPDRes& in,
             struct iso20_ac_der_sae_ActivePowerSupportCPDResType& out) {
    init_iso20_ac_der_sae_ActivePowerSupportCPDResType(&out);
    convert(in.frequency_droop, out.FrequencyDroop);
    convert(in.volt_watt, out.VoltWatt);
    convert(in.constant_watt, out.ConstantWatt);
    convert(in.limit_max_discharge_power, out.LimitMaxDischargePower);
}

template <>
void convert(const struct iso20_ac_der_sae_ReactivePowerSupportCPDResType& in,
             datatypes::sae::ReactivePowerSupportCPDRes& out) {
    convert(in.ConstantPowerFactor, out.constant_power_factor);
    convert(in.VoltVar, out.volt_var);
    convert(in.WattVar, out.watt_var);
    convert(in.ConstantVar, out.constant_var);
}

template <>
void convert(const datatypes::sae::ReactivePowerSupportCPDRes& in,
             struct iso20_ac_der_sae_ReactivePowerSupportCPDResType& out) {
    init_iso20_ac_der_sae_ReactivePowerSupportCPDResType(&out);
    convert(in.constant_power_factor, out.ConstantPowerFactor);
    convert(in.volt_var, out.VoltVar);
    convert(in.watt_var, out.WattVar);
    convert(in.constant_var, out.ConstantVar);
}

template <>
void convert(const struct iso20_ac_der_sae_EnterServiceCPDResType& in, datatypes::sae::EnterServiceCPDRes& out) {
    out.permit_service = in.PermitService;
    convert(in.EnterServiceVoltageHigh, out.enter_service_voltage_high);
    convert(in.EnterServiceVoltageLow, out.enter_service_voltage_low);
    convert(in.EnterServiceFrequencyHigh, out.enter_service_frequency_high);
    convert(in.EnterServiceFrequencyLow, out.enter_service_frequency_low);
    CB2CPP_CONVERT_IF_USED(in.EnterServiceDelay, out.enter_service_delay);
    CB2CPP_CONVERT_IF_USED(in.EnterServiceRandomizedDelay, out.enter_service_randomized_delay);
    CB2CPP_CONVERT_IF_USED(in.EnterServiceRampTime, out.enter_service_ramp_time);
}

template <>
void convert(const datatypes::sae::EnterServiceCPDRes& in, struct iso20_ac_der_sae_EnterServiceCPDResType& out) {
    init_iso20_ac_der_sae_EnterServiceCPDResType(&out);
    out.PermitService = in.permit_service;
    convert(in.enter_service_voltage_high, out.EnterServiceVoltageHigh);
    convert(in.enter_service_voltage_low, out.EnterServiceVoltageLow);
    convert(in.enter_service_frequency_high, out.EnterServiceFrequencyHigh);
    convert(in.enter_service_frequency_low, out.EnterServiceFrequencyLow);
    CPP2CB_CONVERT_IF_USED(in.enter_service_delay, out.EnterServiceDelay);
    CPP2CB_CONVERT_IF_USED(in.enter_service_randomized_delay, out.EnterServiceRandomizedDelay);
    CPP2CB_CONVERT_IF_USED(in.enter_service_ramp_time, out.EnterServiceRampTime);
}

template <>
void convert(const struct iso20_ac_der_sae_DERControlCPDResType& in, datatypes::sae::DERControlCPDRes& out) {
    convert(in.VoltageTrip, out.voltage_trip);
    convert(in.FrequencyTrip, out.frequency_trip);
    convert(in.EnterServiceCPDRes, out.enter_service_cpd_res);
    convert(in.ReactivePowerSupportCPDRes, out.reactive_power_support_cpd_res);
    convert(in.ActivePowerSupportCPDRes, out.active_power_support_cpd_res);
}

template <>
void convert(const datatypes::sae::DERControlCPDRes& in, struct iso20_ac_der_sae_DERControlCPDResType& out) {
    init_iso20_ac_der_sae_DERControlCPDResType(&out);
    convert(in.voltage_trip, out.VoltageTrip);
    convert(in.frequency_trip, out.FrequencyTrip);
    convert(in.enter_service_cpd_res, out.EnterServiceCPDRes);
    convert(in.reactive_power_support_cpd_res, out.ReactivePowerSupportCPDRes);
    convert(in.active_power_support_cpd_res, out.ActivePowerSupportCPDRes);
}

template <>
void convert(const struct iso20_ac_der_sae_EVSEReactivePowerLimitsType& in,
             datatypes::sae::EVSEReactivePowerLimits& out) {
    convert(in.EVSEMaximumVarAbsorptionDuringCharging, out.maximum_var_absorption_during_charging);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumVarAbsorptionDuringCharging_L2, out.maximum_var_absorption_during_charging_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumVarAbsorptionDuringCharging_L3, out.maximum_var_absorption_during_charging_L3);
    convert(in.EVSEMaximumVarInjectionDuringCharging, out.maximum_var_injection_during_charging);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumVarInjectionDuringCharging_L2, out.maximum_var_injection_during_charging_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumVarInjectionDuringCharging_L3, out.maximum_var_injection_during_charging_L3);
    convert(in.EVSEMaximumVarAbsorptionDuringDischarging, out.maximum_var_absorption_during_discharging);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumVarAbsorptionDuringDischarging_L2,
                           out.maximum_var_absorption_during_discharging_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumVarAbsorptionDuringDischarging_L3,
                           out.maximum_var_absorption_during_discharging_L3);
    convert(in.EVSEMaximumVarInjectionDuringDischarging, out.maximum_var_injection_during_discharging);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumVarInjectionDuringDischarging_L2,
                           out.maximum_var_injection_during_discharging_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumVarInjectionDuringDischarging_L3,
                           out.maximum_var_injection_during_discharging_L3);
}

template <>
void convert(const datatypes::sae::EVSEReactivePowerLimits& in,
             struct iso20_ac_der_sae_EVSEReactivePowerLimitsType& out) {
    init_iso20_ac_der_sae_EVSEReactivePowerLimitsType(&out);
    convert(in.maximum_var_absorption_during_charging, out.EVSEMaximumVarAbsorptionDuringCharging);
    CPP2CB_CONVERT_IF_USED(in.maximum_var_absorption_during_charging_L2, out.EVSEMaximumVarAbsorptionDuringCharging_L2);
    CPP2CB_CONVERT_IF_USED(in.maximum_var_absorption_during_charging_L3, out.EVSEMaximumVarAbsorptionDuringCharging_L3);
    convert(in.maximum_var_injection_during_charging, out.EVSEMaximumVarInjectionDuringCharging);
    CPP2CB_CONVERT_IF_USED(in.maximum_var_injection_during_charging_L2, out.EVSEMaximumVarInjectionDuringCharging_L2);
    CPP2CB_CONVERT_IF_USED(in.maximum_var_injection_during_charging_L3, out.EVSEMaximumVarInjectionDuringCharging_L3);
    convert(in.maximum_var_absorption_during_discharging, out.EVSEMaximumVarAbsorptionDuringDischarging);
    CPP2CB_CONVERT_IF_USED(in.maximum_var_absorption_during_discharging_L2,
                           out.EVSEMaximumVarAbsorptionDuringDischarging_L2);
    CPP2CB_CONVERT_IF_USED(in.maximum_var_absorption_during_discharging_L3,
                           out.EVSEMaximumVarAbsorptionDuringDischarging_L3);
    convert(in.maximum_var_injection_during_discharging, out.EVSEMaximumVarInjectionDuringDischarging);
    CPP2CB_CONVERT_IF_USED(in.maximum_var_injection_during_discharging_L2,
                           out.EVSEMaximumVarInjectionDuringDischarging_L2);
    CPP2CB_CONVERT_IF_USED(in.maximum_var_injection_during_discharging_L3,
                           out.EVSEMaximumVarInjectionDuringDischarging_L3);
}

template <> void convert(const struct iso20_ac_der_sae_GridLimitsType& in, datatypes::sae::GridLimits& out) {
    convert(in.GridNominalFrequency, out.nominal_frequency);
    convert(in.GridNominalVoltage, out.nominal_voltage);
    convert(in.GridNominalVoltageOffset, out.nominal_voltage_offset);
    CB2CPP_CONVERT_IF_USED(in.GridMinFrequency, out.min_frequency);
    CB2CPP_CONVERT_IF_USED(in.GridMaxFrequency, out.max_frequency);
    convert(in.GridMaximumVoltage, out.maximum_voltage);
    convert(in.GridMinimumVoltage, out.minimum_voltage);
}

template <> void convert(const datatypes::sae::GridLimits& in, struct iso20_ac_der_sae_GridLimitsType& out) {
    init_iso20_ac_der_sae_GridLimitsType(&out);
    convert(in.nominal_frequency, out.GridNominalFrequency);
    convert(in.nominal_voltage, out.GridNominalVoltage);
    convert(in.nominal_voltage_offset, out.GridNominalVoltageOffset);
    CPP2CB_CONVERT_IF_USED(in.min_frequency, out.GridMinFrequency);
    CPP2CB_CONVERT_IF_USED(in.max_frequency, out.GridMaxFrequency);
    convert(in.maximum_voltage, out.GridMaximumVoltage);
    convert(in.minimum_voltage, out.GridMinimumVoltage);
}

template <> void convert(const struct iso20_ac_der_sae_DER_AC_CPDResEnergyTransferModeType& in, DER_AC_ModeRes& out) {
    // Base AC_CPDResEnergyTransferMode fields
    convert(in.EVSEMaximumChargePower, out.max_charge_power);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumChargePower_L2, out.max_charge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumChargePower_L3, out.max_charge_power_L3);
    convert(in.EVSEMinimumChargePower, out.min_charge_power);
    CB2CPP_CONVERT_IF_USED(in.EVSEMinimumChargePower_L2, out.min_charge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSEMinimumChargePower_L3, out.min_charge_power_L3);
    convert(in.EVSENominalFrequency, out.nominal_frequency);
    CB2CPP_CONVERT_IF_USED(in.MaximumPowerAsymmetry, out.max_power_asymmetry);
    CB2CPP_CONVERT_IF_USED(in.EVSEPowerRampLimitation, out.power_ramp_limitation);
    CB2CPP_CONVERT_IF_USED(in.EVSEPresentActivePower, out.present_active_power);
    CB2CPP_CONVERT_IF_USED(in.EVSEPresentActivePower_L2, out.present_active_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSEPresentActivePower_L3, out.present_active_power_L3);

    // DER-specific fields
    cb_convert_enum(in.EVSEProcessing, out.processing);
    CB2CPP_CONVERT_IF_USED(in.EVSEStatus, out.status);
    convert(in.DERControlCPDRes, out.der_control_cpd_res);
    CB2CPP_CONVERT_IF_USED(in.EVSENominalChargePower, out.nominal_charge_power);
    CB2CPP_CONVERT_IF_USED(in.EVSENominalChargePower_L2, out.nominal_charge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSENominalChargePower_L3, out.nominal_charge_power_L3);
    CB2CPP_CONVERT_IF_USED(in.EVSENominalDischargePower, out.nominal_discharge_power);
    CB2CPP_CONVERT_IF_USED(in.EVSENominalDischargePower_L2, out.nominal_discharge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSENominalDischargePower_L3, out.nominal_discharge_power_L3);
    convert(in.EVSEMaximumDischargePower, out.maximum_discharge_power);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumDischargePower_L2, out.maximum_discharge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumDischargePower_L3, out.maximum_discharge_power_L3);
    convert(in.EVSEReactivePowerLimits, out.reactive_power_limits);
    convert(in.GridLimits, out.grid_limits);
    cb_convert_enum(in.RequiredDEROperatingMode, out.required_der_operating_mode);
    cb_convert_enum(in.GridConnectionMode, out.grid_connection_mode);
    out.update_time = in.EVSEUpdateTime;
}

template <> void convert(const DER_AC_ModeRes& in, struct iso20_ac_der_sae_DER_AC_CPDResEnergyTransferModeType& out) {
    init_iso20_ac_der_sae_DER_AC_CPDResEnergyTransferModeType(&out);

    // Base AC_CPDResEnergyTransferMode fields
    convert(in.max_charge_power, out.EVSEMaximumChargePower);
    CPP2CB_CONVERT_IF_USED(in.max_charge_power_L2, out.EVSEMaximumChargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.max_charge_power_L3, out.EVSEMaximumChargePower_L3);
    convert(in.min_charge_power, out.EVSEMinimumChargePower);
    CPP2CB_CONVERT_IF_USED(in.min_charge_power_L2, out.EVSEMinimumChargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.min_charge_power_L3, out.EVSEMinimumChargePower_L3);
    convert(in.nominal_frequency, out.EVSENominalFrequency);
    CPP2CB_CONVERT_IF_USED(in.max_power_asymmetry, out.MaximumPowerAsymmetry);
    CPP2CB_CONVERT_IF_USED(in.power_ramp_limitation, out.EVSEPowerRampLimitation);
    CPP2CB_CONVERT_IF_USED(in.present_active_power, out.EVSEPresentActivePower);
    CPP2CB_CONVERT_IF_USED(in.present_active_power_L2, out.EVSEPresentActivePower_L2);
    CPP2CB_CONVERT_IF_USED(in.present_active_power_L3, out.EVSEPresentActivePower_L3);

    // DER-specific fields
    cb_convert_enum(in.processing, out.EVSEProcessing);
    CPP2CB_CONVERT_IF_USED(in.status, out.EVSEStatus);
    convert(in.der_control_cpd_res, out.DERControlCPDRes);
    CPP2CB_CONVERT_IF_USED(in.nominal_charge_power, out.EVSENominalChargePower);
    CPP2CB_CONVERT_IF_USED(in.nominal_charge_power_L2, out.EVSENominalChargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.nominal_charge_power_L3, out.EVSENominalChargePower_L3);
    CPP2CB_CONVERT_IF_USED(in.nominal_discharge_power, out.EVSENominalDischargePower);
    CPP2CB_CONVERT_IF_USED(in.nominal_discharge_power_L2, out.EVSENominalDischargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.nominal_discharge_power_L3, out.EVSENominalDischargePower_L3);
    convert(in.maximum_discharge_power, out.EVSEMaximumDischargePower);
    CPP2CB_CONVERT_IF_USED(in.maximum_discharge_power_L2, out.EVSEMaximumDischargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.maximum_discharge_power_L3, out.EVSEMaximumDischargePower_L3);
    convert(in.reactive_power_limits, out.EVSEReactivePowerLimits);
    convert(in.grid_limits, out.GridLimits);
    cb_convert_enum(in.required_der_operating_mode, out.RequiredDEROperatingMode);
    cb_convert_enum(in.grid_connection_mode, out.GridConnectionMode);
    out.EVSEUpdateTime = in.update_time;
}

template <>
void convert(const struct iso20_ac_der_sae_AC_ChargeParameterDiscoveryResType& in,
             DER_SAE_AC_ChargeParameterDiscoveryResponse& out) {
    convert(in.Header, out.header);

    if (in.DER_AC_CPDResEnergyTransferMode_isUsed) {
        convert(in.DER_AC_CPDResEnergyTransferMode, out.transfer_mode);
    } else {
        // TODO(SL): Add warning that a transfer mode other then DER
    }
}

template <> void insert_type(VariantAccess& va, const struct iso20_ac_der_sae_AC_ChargeParameterDiscoveryResType& in) {
    va.insert_type<DER_SAE_AC_ChargeParameterDiscoveryResponse>(in);
}

template <>
void convert(const DER_SAE_AC_ChargeParameterDiscoveryResponse& in,
             struct iso20_ac_der_sae_AC_ChargeParameterDiscoveryResType& out) {

    init_iso20_ac_der_sae_AC_ChargeParameterDiscoveryResType(&out);
    convert(in.header, out.Header);
    cb_convert_enum(in.response_code, out.ResponseCode);

    convert(in.transfer_mode, out.DER_AC_CPDResEnergyTransferMode);
    CB_SET_USED(out.DER_AC_CPDResEnergyTransferMode);
}

template <> int serialize_to_exi(const DER_SAE_AC_ChargeParameterDiscoveryResponse& in, exi_bitstream_t& out) {
    iso20_ac_der_sae_exiDocument doc{};
    init_iso20_ac_der_sae_exiDocument(&doc);
    CB_SET_USED(doc.AC_ChargeParameterDiscoveryRes);
    convert(in, doc.AC_ChargeParameterDiscoveryRes);

    return encode_iso20_ac_der_sae_exiDocument(&out, &doc);
}

template <> size_t serialize(const DER_SAE_AC_ChargeParameterDiscoveryResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_20
