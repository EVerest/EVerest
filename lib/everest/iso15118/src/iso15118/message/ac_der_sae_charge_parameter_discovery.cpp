// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/message/ac_charge_parameter_discovery.hpp>
#include <iso15118/message/ac_der_sae_charge_parameter_discovery.hpp>

#include <iso15118/detail/variant_access.hpp>

#include <cbv2g/iso_20/iso20_AC_DER_SAE_Decoder.h>
#include <cbv2g/iso_20/iso20_AC_DER_SAE_Encoder.h>

namespace iso15118::message_20 {

using DER_AC_ModeReq = datatypes::DER_SAE_AC_CPDReqEnergyTransferMode;
using DER_AC_ModeRes = datatypes::DER_SAE_AC_CPDResEnergyTransferMode;

template <> void convert(const struct iso20_ac_der_sae_DataTupleType& in, datatypes::DataTuple& out) {
    convert(in.xValue, out.x_value);
    convert(in.yValue, out.y_value);
}

template <> void convert(const datatypes::DataTuple& in, struct iso20_ac_der_sae_DataTupleType& out) {
    convert(in.x_value, out.xValue);
    convert(in.y_value, out.yValue);
}

template <>
void convert(const struct iso20_ac_der_sae_CurveDataPointsListType& in, datatypes::CurveDataPointsList& out) {
    out.clear();
    for (size_t i = 0; i < in.CurveDataPoint.arrayLen; ++i) {
        datatypes::DataTuple tuple;
        convert(in.CurveDataPoint.array[i], tuple);
        out.push_back(tuple);
    }
}

template <>
void convert(const datatypes::CurveDataPointsList& in, struct iso20_ac_der_sae_CurveDataPointsListType& out) {
    init_iso20_ac_der_sae_CurveDataPointsListType(&out);
    out.CurveDataPoint.arrayLen = in.size();
    for (size_t i = 0; i < in.size(); i++) {
        convert(in[i], out.CurveDataPoint.array[i]);
    }
}

template <> void convert(const struct iso20_ac_der_sae_DERCurveType& in, datatypes::DERCurve& out) {
    out.enable = static_cast<bool>(in.Enable);
    CB2CPP_ASSIGN_IF_USED(in.Priority, out.priority);
    cb_convert_enum(in.xUnit, out.x_unit);
    cb_convert_enum(in.yUnit, out.y_unit);
    convert(in.CurveDataPoints, out.curve_data_points);
    CB2CPP_CONVERT_IF_USED(in.CurveDataPoints_L2, out.curve_data_points_L2);
    CB2CPP_CONVERT_IF_USED(in.CurveDataPoints_L3, out.curve_data_points_L3);
}

template <> void convert(const datatypes::DERCurve& in, struct iso20_ac_der_sae_DERCurveType& out) {
    init_iso20_ac_der_sae_DERCurveType(&out);
    out.Enable = in.enable;
    CPP2CB_ASSIGN_IF_USED(in.priority, out.Priority);
    cb_convert_enum(in.x_unit, out.xUnit);
    cb_convert_enum(in.y_unit, out.yUnit);
    convert(in.curve_data_points, out.CurveDataPoints);
    CPP2CB_CONVERT_IF_USED(in.curve_data_points_L2, out.CurveDataPoints_L2);
    CPP2CB_CONVERT_IF_USED(in.curve_data_points_L3, out.CurveDataPoints_L3);
}

template <>
void convert(const struct iso20_ac_der_sae_FrequencyDroopSettingsType& in, datatypes::FrequencyDroopSettings& out) {
    convert(in.db, out.db);
    convert(in.DroopFactor, out.droop_factor);
    CB2CPP_CONVERT_IF_USED(in.DroopFactor_L2, out.droop_factor_L2);
    CB2CPP_CONVERT_IF_USED(in.DroopFactor_L3, out.droop_factor_L3);
    cb_convert_enum(in.PowerReference, out.power_reference);
    if (in.PowerReference_L2_isUsed) {
        cb_convert_enum(in.PowerReference_L2, out.power_reference_L2.emplace());
    }
    if (in.PowerReference_L3_isUsed) {
        cb_convert_enum(in.PowerReference_L3, out.power_reference_L3.emplace());
    }
    convert(in.OpenLoopResponseTime, out.open_loop_response_time);
}

template <>
void convert(const datatypes::FrequencyDroopSettings& in, struct iso20_ac_der_sae_FrequencyDroopSettingsType& out) {
    init_iso20_ac_der_sae_FrequencyDroopSettingsType(&out);
    convert(in.db, out.db);
    convert(in.droop_factor, out.DroopFactor);
    CPP2CB_CONVERT_IF_USED(in.droop_factor_L2, out.DroopFactor_L2);
    CPP2CB_CONVERT_IF_USED(in.droop_factor_L3, out.DroopFactor_L3);
    cb_convert_enum(in.power_reference, out.PowerReference);
    if (in.power_reference_L2.has_value()) {
        cb_convert_enum(in.power_reference_L2.value(), out.PowerReference_L2);
        CB_SET_USED(out.PowerReference_L2);
    }
    if (in.power_reference_L3.has_value()) {
        cb_convert_enum(in.power_reference_L3.value(), out.PowerReference_L3);
        CB_SET_USED(out.PowerReference_L3);
    }
    convert(in.open_loop_response_time, out.OpenLoopResponseTime);
}

template <> void convert(const struct iso20_ac_der_sae_FrequencyDroopType& in, datatypes::FrequencyDroop& out) {
    out.enable = static_cast<bool>(in.Enable);
    CB2CPP_ASSIGN_IF_USED(in.Priority, out.priority);
    CB2CPP_CONVERT_IF_USED(in.OverFrequencyDroop, out.over_frequency_droop);
    CB2CPP_CONVERT_IF_USED(in.UnderFrequencyDroop, out.under_frequency_droop);
}

template <> void convert(const datatypes::FrequencyDroop& in, struct iso20_ac_der_sae_FrequencyDroopType& out) {
    init_iso20_ac_der_sae_FrequencyDroopType(&out);
    out.Enable = in.enable;
    CPP2CB_ASSIGN_IF_USED(in.priority, out.Priority);
    CPP2CB_CONVERT_IF_USED(in.over_frequency_droop, out.OverFrequencyDroop);
    CPP2CB_CONVERT_IF_USED(in.under_frequency_droop, out.UnderFrequencyDroop);
}

template <> void convert(const struct iso20_ac_der_sae_VoltWattType& in, datatypes::VoltWatt& out) {
    out.enable = static_cast<bool>(in.Enable);
    CB2CPP_ASSIGN_IF_USED(in.Priority, out.priority);
    cb_convert_enum(in.xUnit, out.x_unit);
    cb_convert_enum(in.yUnit, out.y_unit);
    convert(in.CurveDataPoints, out.curve_data_points);
    CB2CPP_CONVERT_IF_USED(in.CurveDataPoints_L2, out.curve_data_points_L2);
    CB2CPP_CONVERT_IF_USED(in.CurveDataPoints_L3, out.curve_data_points_L3);
    convert(in.OpenLoopResponseTime, out.open_loop_response_time);
    CB2CPP_ASSIGN_IF_USED(in.TimeConstantPT1, out.time_constant_pt1);
}

template <> void convert(const datatypes::VoltWatt& in, struct iso20_ac_der_sae_VoltWattType& out) {
    init_iso20_ac_der_sae_VoltWattType(&out);
    out.Enable = in.enable;
    CPP2CB_ASSIGN_IF_USED(in.priority, out.Priority);
    cb_convert_enum(in.x_unit, out.xUnit);
    cb_convert_enum(in.y_unit, out.yUnit);
    convert(in.curve_data_points, out.CurveDataPoints);
    CPP2CB_CONVERT_IF_USED(in.curve_data_points_L2, out.CurveDataPoints_L2);
    CPP2CB_CONVERT_IF_USED(in.curve_data_points_L3, out.CurveDataPoints_L3);
    convert(in.open_loop_response_time, out.OpenLoopResponseTime);
    CPP2CB_ASSIGN_IF_USED(in.time_constant_pt1, out.TimeConstantPT1);
}

template <> void convert(const struct iso20_ac_der_sae_ConstantWattType& in, datatypes::ConstantWatt& out) {
    out.enable = static_cast<bool>(in.Enable);
    CB2CPP_ASSIGN_IF_USED(in.Priority, out.priority);
    convert(in.WattSetpoint, out.watt_setpoint);
    CB2CPP_CONVERT_IF_USED(in.WattSetpoint_L2, out.watt_setpoint_L2);
    CB2CPP_CONVERT_IF_USED(in.WattSetpoint_L3, out.watt_setpoint_L3);
    cb_convert_enum(in.Unit, out.unit);
}

template <> void convert(const datatypes::ConstantWatt& in, struct iso20_ac_der_sae_ConstantWattType& out) {
    init_iso20_ac_der_sae_ConstantWattType(&out);
    out.Enable = in.enable;
    CPP2CB_ASSIGN_IF_USED(in.priority, out.Priority);
    convert(in.watt_setpoint, out.WattSetpoint);
    CPP2CB_CONVERT_IF_USED(in.watt_setpoint_L2, out.WattSetpoint_L2);
    CPP2CB_CONVERT_IF_USED(in.watt_setpoint_L3, out.WattSetpoint_L3);
    cb_convert_enum(in.unit, out.Unit);
}

template <>
void convert(const struct iso20_ac_der_sae_LimitMaxDischargePowerType& in, datatypes::LimitMaxDischargePower& out) {
    out.enable = static_cast<bool>(in.Enable);
    CB2CPP_ASSIGN_IF_USED(in.Priority, out.priority);
    out.percentage_value = in.PercentageValue;
    CB2CPP_ASSIGN_IF_USED(in.PercentageValue_L2, out.percentage_value_L2);
    CB2CPP_ASSIGN_IF_USED(in.PercentageValue_L3, out.percentage_value_L3);
    CB2CPP_CONVERT_IF_USED(in.OpenLoopResponseTime, out.open_loop_response_time);
}

template <>
void convert(const datatypes::LimitMaxDischargePower& in, struct iso20_ac_der_sae_LimitMaxDischargePowerType& out) {
    init_iso20_ac_der_sae_LimitMaxDischargePowerType(&out);
    out.Enable = in.enable;
    CPP2CB_ASSIGN_IF_USED(in.priority, out.Priority);
    out.PercentageValue = in.percentage_value;
    CPP2CB_ASSIGN_IF_USED(in.percentage_value_L2, out.PercentageValue_L2);
    CPP2CB_ASSIGN_IF_USED(in.percentage_value_L3, out.PercentageValue_L3);
    CPP2CB_CONVERT_IF_USED(in.open_loop_response_time, out.OpenLoopResponseTime);
}

template <>
void convert(const struct iso20_ac_der_sae_ActivePowerSupportCPDResType& in, datatypes::ActivePowerSupportCPDRes& out) {
    convert(in.FrequencyDroop, out.frequency_droop);
    convert(in.VoltWatt, out.volt_watt);
    convert(in.ConstantWatt, out.constant_watt);
    convert(in.LimitMaxDischargePower, out.limit_max_discharge_power);
}

template <>
void convert(const datatypes::ActivePowerSupportCPDRes& in, struct iso20_ac_der_sae_ActivePowerSupportCPDResType& out) {
    init_iso20_ac_der_sae_ActivePowerSupportCPDResType(&out);
    convert(in.frequency_droop, out.FrequencyDroop);
    convert(in.volt_watt, out.VoltWatt);
    convert(in.constant_watt, out.ConstantWatt);
    convert(in.limit_max_discharge_power, out.LimitMaxDischargePower);
}

template <>
void convert(const struct iso20_ac_der_sae_ConstantPowerFactorType& in, datatypes::ConstantPowerFactor& out) {
    out.enable = static_cast<bool>(in.Enable);
    CB2CPP_ASSIGN_IF_USED(in.Priority, out.priority);
    convert(in.PowerFactorValue, out.power_factor_value);
    CB2CPP_CONVERT_IF_USED(in.PowerFactorValue_L2, out.power_factor_value_L2);
    CB2CPP_CONVERT_IF_USED(in.PowerFactorValue_L3, out.power_factor_value_L3);
    cb_convert_enum(in.PowerFactorExcitation, out.power_factor_excitation);
    if (in.PowerFactorExcitation_L2_isUsed) {
        cb_convert_enum(in.PowerFactorExcitation_L2, out.power_factor_excitation_L2.emplace());
    }
    if (in.PowerFactorExcitation_L3_isUsed) {
        cb_convert_enum(in.PowerFactorExcitation_L3, out.power_factor_excitation_L3.emplace());
    }
}

template <>
void convert(const datatypes::ConstantPowerFactor& in, struct iso20_ac_der_sae_ConstantPowerFactorType& out) {
    init_iso20_ac_der_sae_ConstantPowerFactorType(&out);
    out.Enable = in.enable;
    CPP2CB_ASSIGN_IF_USED(in.priority, out.Priority);
    convert(in.power_factor_value, out.PowerFactorValue);
    CPP2CB_CONVERT_IF_USED(in.power_factor_value_L2, out.PowerFactorValue_L2);
    CPP2CB_CONVERT_IF_USED(in.power_factor_value_L3, out.PowerFactorValue_L3);
    cb_convert_enum(in.power_factor_excitation, out.PowerFactorExcitation);
    if (in.power_factor_excitation_L2.has_value()) {
        cb_convert_enum(in.power_factor_excitation_L2.value(), out.PowerFactorExcitation_L2);
        CB_SET_USED(out.PowerFactorExcitation_L2);
    }
    if (in.power_factor_excitation_L3.has_value()) {
        cb_convert_enum(in.power_factor_excitation_L3.value(), out.PowerFactorExcitation_L3);
        CB_SET_USED(out.PowerFactorExcitation_L3);
    }
}

template <> void convert(const struct iso20_ac_der_sae_WattVarType& in, datatypes::WattVar& out) {
    out.enable = in.Enable;
    CB2CPP_ASSIGN_IF_USED(in.Priority, out.priority);
    cb_convert_enum(in.xUnit, out.x_unit);
    cb_convert_enum(in.yUnit, out.y_unit);
    convert(in.CurveDataPoints, out.curve_data_points);
    CB2CPP_CONVERT_IF_USED(in.CurveDataPoints_L2, out.curve_data_points_L2);
    CB2CPP_CONVERT_IF_USED(in.CurveDataPoints_L3, out.curve_data_points_L3);
    CB2CPP_CONVERT_IF_USED(in.OpenLoopResponseTime, out.open_loop_response_time);
    CB2CPP_ASSIGN_IF_USED(in.TimeConstantPT1, out.time_constant_pt1);
}

template <> void convert(const datatypes::WattVar& in, struct iso20_ac_der_sae_WattVarType& out) {
    init_iso20_ac_der_sae_WattVarType(&out);
    out.Enable = in.enable;
    CPP2CB_ASSIGN_IF_USED(in.priority, out.Priority);
    cb_convert_enum(in.x_unit, out.xUnit);
    cb_convert_enum(in.y_unit, out.yUnit);
    convert(in.curve_data_points, out.CurveDataPoints);
    CPP2CB_CONVERT_IF_USED(in.curve_data_points_L2, out.CurveDataPoints_L2);
    CPP2CB_CONVERT_IF_USED(in.curve_data_points_L3, out.CurveDataPoints_L3);
    CPP2CB_CONVERT_IF_USED(in.open_loop_response_time, out.OpenLoopResponseTime);
    CPP2CB_ASSIGN_IF_USED(in.time_constant_pt1, out.TimeConstantPT1);
}

template <> void convert(const struct iso20_ac_der_sae_VoltVarType& in, datatypes::VoltVar& out) {
    out.enable = in.Enable;
    CB2CPP_ASSIGN_IF_USED(in.Priority, out.priority);
    cb_convert_enum(in.xUnit, out.x_unit);
    cb_convert_enum(in.yUnit, out.y_unit);
    convert(in.CurveDataPoints, out.curve_data_points);
    CB2CPP_CONVERT_IF_USED(in.CurveDataPoints_L2, out.curve_data_points_L2);
    CB2CPP_CONVERT_IF_USED(in.CurveDataPoints_L3, out.curve_data_points_L3);
    convert(in.OpenLoopResponseTime, out.open_loop_response_time);
    CB2CPP_ASSIGN_IF_USED(in.TimeConstantPT1, out.time_constant_pt1);
    convert(in.ReferenceVoltage, out.reference_voltage);
    out.autonomous_reference_voltage_adjustment_enable = in.AutonomousReferenceVoltageAdjustmentEnable;
    out.reference_voltage_adjustment_time_constant = in.ReferenceVoltageAdjustmentTimeConstant;
}

template <> void convert(const datatypes::VoltVar& in, struct iso20_ac_der_sae_VoltVarType& out) {
    init_iso20_ac_der_sae_VoltVarType(&out);
    out.Enable = in.enable;
    CPP2CB_ASSIGN_IF_USED(in.priority, out.Priority);
    cb_convert_enum(in.x_unit, out.xUnit);
    cb_convert_enum(in.y_unit, out.yUnit);
    convert(in.curve_data_points, out.CurveDataPoints);
    CPP2CB_CONVERT_IF_USED(in.curve_data_points_L2, out.CurveDataPoints_L2);
    CPP2CB_CONVERT_IF_USED(in.curve_data_points_L3, out.CurveDataPoints_L3);
    convert(in.open_loop_response_time, out.OpenLoopResponseTime);
    CPP2CB_ASSIGN_IF_USED(in.time_constant_pt1, out.TimeConstantPT1);
    convert(in.reference_voltage, out.ReferenceVoltage);
    out.AutonomousReferenceVoltageAdjustmentEnable = in.autonomous_reference_voltage_adjustment_enable;
    out.ReferenceVoltageAdjustmentTimeConstant = in.reference_voltage_adjustment_time_constant;
}

template <> void convert(const struct iso20_ac_der_sae_ConstantVarType& in, datatypes::ConstantVar& out) {
    out.enable = in.Enable;
    CB2CPP_ASSIGN_IF_USED(in.Priority, out.priority);
    convert(in.VarSetpoint, out.var_setpoint);
    CB2CPP_CONVERT_IF_USED(in.VarSetpoint_L2, out.var_setpoint_L2);
    CB2CPP_CONVERT_IF_USED(in.VarSetpoint_L3, out.var_setpoint_L3);
    cb_convert_enum(in.Unit, out.unit);
}

template <> void convert(const datatypes::ConstantVar& in, struct iso20_ac_der_sae_ConstantVarType& out) {
    init_iso20_ac_der_sae_ConstantVarType(&out);
    out.Enable = in.enable;
    CPP2CB_ASSIGN_IF_USED(in.priority, out.Priority);
    convert(in.var_setpoint, out.VarSetpoint);
    CPP2CB_CONVERT_IF_USED(in.var_setpoint_L2, out.VarSetpoint_L2);
    CPP2CB_CONVERT_IF_USED(in.var_setpoint_L3, out.VarSetpoint_L3);
    cb_convert_enum(in.unit, out.Unit);
}

template <>
void convert(const struct iso20_ac_der_sae_ReactivePowerSupportCPDResType& in,
             datatypes::ReactivePowerSupportCPDRes& out) {
    convert(in.ConstantPowerFactor, out.constant_power_factor);
    convert(in.VoltVar, out.volt_var);
    convert(in.WattVar, out.watt_var);
    convert(in.ConstantVar, out.constant_var);
}

template <>
void convert(const datatypes::ReactivePowerSupportCPDRes& in,
             struct iso20_ac_der_sae_ReactivePowerSupportCPDResType& out) {
    init_iso20_ac_der_sae_ReactivePowerSupportCPDResType(&out);
    convert(in.constant_power_factor, out.ConstantPowerFactor);
    convert(in.volt_var, out.VoltVar);
    convert(in.watt_var, out.WattVar);
    convert(in.constant_var, out.ConstantVar);
}

template <> void convert(const struct iso20_ac_der_sae_VoltageTripType& in, datatypes::VoltageTrip& out) {
    convert(in.OverVoltageMustTripCurve, out.over_voltage_must_trip_curve);
    convert(in.UnderVoltageMustTripCurve, out.under_voltage_must_trip_curve);
    CB2CPP_CONVERT_IF_USED(in.OverVoltageMomentaryCessationTripCurve, out.over_voltage_momentary_cessation_trip_curve);
    CB2CPP_CONVERT_IF_USED(in.UnderVoltageMomentaryCessationTripCurve,
                           out.under_voltage_momentary_cessation_trip_curve);
    CB2CPP_CONVERT_IF_USED(in.OverVoltageMayTripCurve, out.over_voltage_may_trip_curve);
    CB2CPP_CONVERT_IF_USED(in.UnderVoltageMayTripCurve, out.under_voltage_may_trip_curve);
}

template <> void convert(const datatypes::VoltageTrip& in, struct iso20_ac_der_sae_VoltageTripType& out) {
    init_iso20_ac_der_sae_VoltageTripType(&out);
    convert(in.over_voltage_must_trip_curve, out.OverVoltageMustTripCurve);
    convert(in.under_voltage_must_trip_curve, out.UnderVoltageMustTripCurve);
    CPP2CB_CONVERT_IF_USED(in.over_voltage_momentary_cessation_trip_curve, out.OverVoltageMomentaryCessationTripCurve);
    CPP2CB_CONVERT_IF_USED(in.under_voltage_momentary_cessation_trip_curve,
                           out.UnderVoltageMomentaryCessationTripCurve);
    CPP2CB_CONVERT_IF_USED(in.over_voltage_may_trip_curve, out.OverVoltageMayTripCurve);
    CPP2CB_CONVERT_IF_USED(in.under_voltage_may_trip_curve, out.UnderVoltageMayTripCurve);
}

template <> void convert(const struct iso20_ac_der_sae_FrequencyTripType& in, datatypes::FrequencyTrip& out) {
    convert(in.OverFrequencyMustTripCurve, out.over_frequency_must_trip_curve);
    convert(in.UnderFrequencyMustTripCurve, out.under_frequency_must_trip_curve);
    CB2CPP_CONVERT_IF_USED(in.OverFrequencyMayTripCurve, out.over_frequency_may_trip_curve);
    CB2CPP_CONVERT_IF_USED(in.UnderFrequencyMayTripCurve, out.under_frequency_may_trip_curve);
}

template <> void convert(const datatypes::FrequencyTrip& in, struct iso20_ac_der_sae_FrequencyTripType& out) {
    init_iso20_ac_der_sae_FrequencyTripType(&out);
    convert(in.over_frequency_must_trip_curve, out.OverFrequencyMustTripCurve);
    convert(in.under_frequency_must_trip_curve, out.UnderFrequencyMustTripCurve);
    CPP2CB_CONVERT_IF_USED(in.over_frequency_may_trip_curve, out.OverFrequencyMayTripCurve);
    CPP2CB_CONVERT_IF_USED(in.under_frequency_may_trip_curve, out.UnderFrequencyMayTripCurve);
}

template <> void convert(const struct iso20_ac_der_sae_EnterServiceCPDResType& in, datatypes::EnterServiceCPDRes& out) {
    out.permit_service = in.PermitService;
    convert(in.EnterServiceVoltageHigh, out.enter_service_voltage_high);
    convert(in.EnterServiceVoltageLow, out.enter_service_voltage_low);
    convert(in.EnterServiceFrequencyHigh, out.enter_service_frequency_high);
    convert(in.EnterServiceFrequencyLow, out.enter_service_frequency_low);
    CB2CPP_CONVERT_IF_USED(in.EnterServiceDelay, out.enter_service_delay);
    CB2CPP_CONVERT_IF_USED(in.EnterServiceRandomizedDelay, out.enter_service_randomized_delay);
    CB2CPP_CONVERT_IF_USED(in.EnterServiceRampTime, out.enter_service_ramp_time);
}

template <> void convert(const datatypes::EnterServiceCPDRes& in, struct iso20_ac_der_sae_EnterServiceCPDResType& out) {
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

template <> void convert(const struct iso20_ac_der_sae_DERControlCPDResType& in, datatypes::DERControlCPDRes& out) {
    convert(in.VoltageTrip, out.voltage_trip);
    convert(in.FrequencyTrip, out.frequency_trip);
    convert(in.EnterServiceCPDRes, out.enter_service_cpd_res);
    convert(in.ReactivePowerSupportCPDRes, out.reactive_power_support_cpd_res);
    convert(in.ActivePowerSupportCPDRes, out.active_power_support_cpd_res);
}

template <> void convert(const datatypes::DERControlCPDRes& in, struct iso20_ac_der_sae_DERControlCPDResType& out) {
    init_iso20_ac_der_sae_DERControlCPDResType(&out);
    convert(in.voltage_trip, out.VoltageTrip);
    convert(in.frequency_trip, out.FrequencyTrip);
    convert(in.enter_service_cpd_res, out.EnterServiceCPDRes);
    convert(in.reactive_power_support_cpd_res, out.ReactivePowerSupportCPDRes);
    convert(in.active_power_support_cpd_res, out.ActivePowerSupportCPDRes);
}

template <>
void convert(const struct iso20_ac_der_sae_EVSEReactivePowerLimitsType& in, datatypes::EVSEReactivePowerLimits& out) {
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
void convert(const datatypes::EVSEReactivePowerLimits& in, struct iso20_ac_der_sae_EVSEReactivePowerLimitsType& out) {
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

template <> void convert(const struct iso20_ac_der_sae_GridLimitsType& in, datatypes::GridLimits& out) {
    convert(in.GridNominalFrequency, out.nominal_frequency);
    convert(in.GridNominalVoltage, out.nominal_voltage);
    convert(in.GridNominalVoltageOffset, out.nominal_voltage_offset);
    CB2CPP_CONVERT_IF_USED(in.GridMinFrequency, out.min_frequency);
    CB2CPP_CONVERT_IF_USED(in.GridMaxFrequency, out.max_frequency);
    convert(in.GridMaximumVoltage, out.maximum_voltage);
    convert(in.GridMinimumVoltage, out.minimum_voltage);
}

template <> void convert(const datatypes::GridLimits& in, struct iso20_ac_der_sae_GridLimitsType& out) {
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
