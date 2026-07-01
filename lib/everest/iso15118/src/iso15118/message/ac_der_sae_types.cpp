// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/message/ac_der_sae_types.hpp>

#include <iso15118/detail/cb_exi.hpp>
#include <iso15118/message/variant.hpp>

#include <cbv2g/iso_20/iso20_AC_DER_SAE_Decoder.h>
#include <cbv2g/iso_20/iso20_AC_DER_SAE_Encoder.h>

namespace iso15118::message_20 {

template <> void convert(const struct iso20_ac_der_sae_DataTupleType& in, datatypes::sae::DataTuple& out) {
    convert(in.xValue, out.x_value);
    convert(in.yValue, out.y_value);
}

template <> void convert(const datatypes::sae::DataTuple& in, struct iso20_ac_der_sae_DataTupleType& out) {
    convert(in.x_value, out.xValue);
    convert(in.y_value, out.yValue);
}

template <>
void convert(const struct iso20_ac_der_sae_CurveDataPointsListType& in, datatypes::sae::CurveDataPointsList& out) {
    out.clear();
    for (size_t i = 0; i < in.CurveDataPoint.arrayLen; ++i) {
        datatypes::sae::DataTuple tuple;
        convert(in.CurveDataPoint.array[i], tuple);
        out.push_back(tuple);
    }
}

template <>
void convert(const datatypes::sae::CurveDataPointsList& in, struct iso20_ac_der_sae_CurveDataPointsListType& out) {
    init_iso20_ac_der_sae_CurveDataPointsListType(&out);

    const size_t in_len =
        (in.size() <= iso20_ac_der_sae_DataTupleType_10_ARRAY_SIZE ? in.size()
                                                                   : iso20_ac_der_sae_DataTupleType_10_ARRAY_SIZE);
    out.CurveDataPoint.arrayLen = in_len;

    for (size_t i = 0; i < in_len; i++) {
        convert(in[i], out.CurveDataPoint.array[i]);
    }
}

template <> void convert(const struct iso20_ac_der_sae_DERCurveType& in, datatypes::sae::DERCurve& out) {
    out.enable = static_cast<bool>(in.Enable);
    CB2CPP_ASSIGN_IF_USED(in.Priority, out.priority);
    cb_convert_enum(in.xUnit, out.x_unit);
    cb_convert_enum(in.yUnit, out.y_unit);
    convert(in.CurveDataPoints, out.curve_data_points);
    CB2CPP_CONVERT_IF_USED(in.CurveDataPoints_L2, out.curve_data_points_L2);
    CB2CPP_CONVERT_IF_USED(in.CurveDataPoints_L3, out.curve_data_points_L3);
}

template <> void convert(const datatypes::sae::DERCurve& in, struct iso20_ac_der_sae_DERCurveType& out) {
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
void convert(const struct iso20_ac_der_sae_FrequencyDroopSettingsType& in,
             datatypes::sae::FrequencyDroopSettings& out) {
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
void convert(const datatypes::sae::FrequencyDroopSettings& in,
             struct iso20_ac_der_sae_FrequencyDroopSettingsType& out) {
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

template <> void convert(const struct iso20_ac_der_sae_FrequencyDroopType& in, datatypes::sae::FrequencyDroop& out) {
    out.enable = static_cast<bool>(in.Enable);
    CB2CPP_ASSIGN_IF_USED(in.Priority, out.priority);
    CB2CPP_CONVERT_IF_USED(in.OverFrequencyDroop, out.over_frequency_droop);
    CB2CPP_CONVERT_IF_USED(in.UnderFrequencyDroop, out.under_frequency_droop);
}

template <> void convert(const datatypes::sae::FrequencyDroop& in, struct iso20_ac_der_sae_FrequencyDroopType& out) {
    init_iso20_ac_der_sae_FrequencyDroopType(&out);
    out.Enable = in.enable;
    CPP2CB_ASSIGN_IF_USED(in.priority, out.Priority);
    CPP2CB_CONVERT_IF_USED(in.over_frequency_droop, out.OverFrequencyDroop);
    CPP2CB_CONVERT_IF_USED(in.under_frequency_droop, out.UnderFrequencyDroop);
}

template <> void convert(const struct iso20_ac_der_sae_VoltWattType& in, datatypes::sae::VoltWatt& out) {
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

template <> void convert(const datatypes::sae::VoltWatt& in, struct iso20_ac_der_sae_VoltWattType& out) {
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

template <> void convert(const struct iso20_ac_der_sae_ConstantWattType& in, datatypes::sae::ConstantWatt& out) {
    out.enable = static_cast<bool>(in.Enable);
    CB2CPP_ASSIGN_IF_USED(in.Priority, out.priority);
    convert(in.WattSetpoint, out.watt_setpoint);
    CB2CPP_CONVERT_IF_USED(in.WattSetpoint_L2, out.watt_setpoint_L2);
    CB2CPP_CONVERT_IF_USED(in.WattSetpoint_L3, out.watt_setpoint_L3);
    cb_convert_enum(in.Unit, out.unit);
}

template <> void convert(const datatypes::sae::ConstantWatt& in, struct iso20_ac_der_sae_ConstantWattType& out) {
    init_iso20_ac_der_sae_ConstantWattType(&out);
    out.Enable = in.enable;
    CPP2CB_ASSIGN_IF_USED(in.priority, out.Priority);
    convert(in.watt_setpoint, out.WattSetpoint);
    CPP2CB_CONVERT_IF_USED(in.watt_setpoint_L2, out.WattSetpoint_L2);
    CPP2CB_CONVERT_IF_USED(in.watt_setpoint_L3, out.WattSetpoint_L3);
    cb_convert_enum(in.unit, out.Unit);
}

template <>
void convert(const struct iso20_ac_der_sae_LimitMaxDischargePowerType& in,
             datatypes::sae::LimitMaxDischargePower& out) {
    out.enable = static_cast<bool>(in.Enable);
    CB2CPP_ASSIGN_IF_USED(in.Priority, out.priority);
    out.percentage_value = in.PercentageValue;
    CB2CPP_ASSIGN_IF_USED(in.PercentageValue_L2, out.percentage_value_L2);
    CB2CPP_ASSIGN_IF_USED(in.PercentageValue_L3, out.percentage_value_L3);
    CB2CPP_CONVERT_IF_USED(in.OpenLoopResponseTime, out.open_loop_response_time);
}

template <>
void convert(const datatypes::sae::LimitMaxDischargePower& in,
             struct iso20_ac_der_sae_LimitMaxDischargePowerType& out) {
    init_iso20_ac_der_sae_LimitMaxDischargePowerType(&out);
    out.Enable = in.enable;
    CPP2CB_ASSIGN_IF_USED(in.priority, out.Priority);
    out.PercentageValue = in.percentage_value;
    CPP2CB_ASSIGN_IF_USED(in.percentage_value_L2, out.PercentageValue_L2);
    CPP2CB_ASSIGN_IF_USED(in.percentage_value_L3, out.PercentageValue_L3);
    CPP2CB_CONVERT_IF_USED(in.open_loop_response_time, out.OpenLoopResponseTime);
}

template <>
void convert(const struct iso20_ac_der_sae_ConstantPowerFactorType& in, datatypes::sae::ConstantPowerFactor& out) {
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
void convert(const datatypes::sae::ConstantPowerFactor& in, struct iso20_ac_der_sae_ConstantPowerFactorType& out) {
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

template <> void convert(const struct iso20_ac_der_sae_WattVarType& in, datatypes::sae::WattVar& out) {
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

template <> void convert(const datatypes::sae::WattVar& in, struct iso20_ac_der_sae_WattVarType& out) {
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

template <> void convert(const struct iso20_ac_der_sae_VoltVarType& in, datatypes::sae::VoltVar& out) {
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

template <> void convert(const datatypes::sae::VoltVar& in, struct iso20_ac_der_sae_VoltVarType& out) {
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

template <> void convert(const struct iso20_ac_der_sae_ConstantVarType& in, datatypes::sae::ConstantVar& out) {
    out.enable = in.Enable;
    CB2CPP_ASSIGN_IF_USED(in.Priority, out.priority);
    convert(in.VarSetpoint, out.var_setpoint);
    CB2CPP_CONVERT_IF_USED(in.VarSetpoint_L2, out.var_setpoint_L2);
    CB2CPP_CONVERT_IF_USED(in.VarSetpoint_L3, out.var_setpoint_L3);
    cb_convert_enum(in.Unit, out.unit);
}

template <> void convert(const datatypes::sae::ConstantVar& in, struct iso20_ac_der_sae_ConstantVarType& out) {
    init_iso20_ac_der_sae_ConstantVarType(&out);
    out.Enable = in.enable;
    CPP2CB_ASSIGN_IF_USED(in.priority, out.Priority);
    convert(in.var_setpoint, out.VarSetpoint);
    CPP2CB_CONVERT_IF_USED(in.var_setpoint_L2, out.VarSetpoint_L2);
    CPP2CB_CONVERT_IF_USED(in.var_setpoint_L3, out.VarSetpoint_L3);
    cb_convert_enum(in.unit, out.Unit);
}

template <> void convert(const struct iso20_ac_der_sae_VoltageTripType& in, datatypes::sae::VoltageTrip& out) {
    convert(in.OverVoltageMustTripCurve, out.over_voltage_must_trip_curve);
    convert(in.UnderVoltageMustTripCurve, out.under_voltage_must_trip_curve);
    CB2CPP_CONVERT_IF_USED(in.OverVoltageMomentaryCessationTripCurve, out.over_voltage_momentary_cessation_trip_curve);
    CB2CPP_CONVERT_IF_USED(in.UnderVoltageMomentaryCessationTripCurve,
                           out.under_voltage_momentary_cessation_trip_curve);
    CB2CPP_CONVERT_IF_USED(in.OverVoltageMayTripCurve, out.over_voltage_may_trip_curve);
    CB2CPP_CONVERT_IF_USED(in.UnderVoltageMayTripCurve, out.under_voltage_may_trip_curve);
}

template <> void convert(const datatypes::sae::VoltageTrip& in, struct iso20_ac_der_sae_VoltageTripType& out) {
    init_iso20_ac_der_sae_VoltageTripType(&out);
    convert(in.over_voltage_must_trip_curve, out.OverVoltageMustTripCurve);
    convert(in.under_voltage_must_trip_curve, out.UnderVoltageMustTripCurve);
    CPP2CB_CONVERT_IF_USED(in.over_voltage_momentary_cessation_trip_curve, out.OverVoltageMomentaryCessationTripCurve);
    CPP2CB_CONVERT_IF_USED(in.under_voltage_momentary_cessation_trip_curve,
                           out.UnderVoltageMomentaryCessationTripCurve);
    CPP2CB_CONVERT_IF_USED(in.over_voltage_may_trip_curve, out.OverVoltageMayTripCurve);
    CPP2CB_CONVERT_IF_USED(in.under_voltage_may_trip_curve, out.UnderVoltageMayTripCurve);
}

template <> void convert(const struct iso20_ac_der_sae_FrequencyTripType& in, datatypes::sae::FrequencyTrip& out) {
    convert(in.OverFrequencyMustTripCurve, out.over_frequency_must_trip_curve);
    convert(in.UnderFrequencyMustTripCurve, out.under_frequency_must_trip_curve);
    CB2CPP_CONVERT_IF_USED(in.OverFrequencyMayTripCurve, out.over_frequency_may_trip_curve);
    CB2CPP_CONVERT_IF_USED(in.UnderFrequencyMayTripCurve, out.under_frequency_may_trip_curve);
}

template <> void convert(const datatypes::sae::FrequencyTrip& in, struct iso20_ac_der_sae_FrequencyTripType& out) {
    init_iso20_ac_der_sae_FrequencyTripType(&out);
    convert(in.over_frequency_must_trip_curve, out.OverFrequencyMustTripCurve);
    convert(in.under_frequency_must_trip_curve, out.UnderFrequencyMustTripCurve);
    CPP2CB_CONVERT_IF_USED(in.over_frequency_may_trip_curve, out.OverFrequencyMayTripCurve);
    CPP2CB_CONVERT_IF_USED(in.under_frequency_may_trip_curve, out.UnderFrequencyMayTripCurve);
}

} // namespace iso15118::message_20
