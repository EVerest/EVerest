// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/message/ac_der_iec_charge_loop.hpp>

#include <type_traits>

#include <iso15118/detail/variant_access.hpp>

#include <cbv2g/iso_20/iso20_AC_DER_IEC_Decoder.h>
#include <cbv2g/iso_20/iso20_AC_DER_IEC_Encoder.h>

namespace iso15118::message_20 {

// Request
template <> void convert(const struct iso20_ac_der_iec_DisplayParametersType& in, datatypes::DisplayParameters& out) {
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

template <> void convert(const datatypes::DisplayParameters& in, struct iso20_ac_der_iec_DisplayParametersType& out) {
    init_iso20_ac_der_iec_DisplayParametersType(&out);

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

template <>
void convert(const datatypes::DER_Dynamic_AC_CLReqControlMode& in,
             struct iso20_ac_der_iec_DER_Dynamic_AC_CLReqControlModeType& out) {
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

    convert(in.max_discharge_power, out.EVMaximumDischargePower);
    CPP2CB_CONVERT_IF_USED(in.max_discharge_power_L2, out.EVMaximumDischargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.max_discharge_power_L3, out.EVMaximumDischargePower_L3);
    convert(in.min_discharge_power, out.EVMinimumDischargePower);
    CPP2CB_CONVERT_IF_USED(in.min_discharge_power_L2, out.EVMinimumDischargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.min_discharge_power_L3, out.EVMinimumDischargePower_L3);

    CPP2CB_CONVERT_IF_USED(in.max_charge_reactive_power, out.EVMaximumChargeReactivePower);
    CPP2CB_CONVERT_IF_USED(in.max_charge_reactive_power_L2, out.EVMaximumChargeReactivePower_L2);
    CPP2CB_CONVERT_IF_USED(in.max_charge_reactive_power_L3, out.EVMaximumChargeReactivePower_L3);
    CPP2CB_CONVERT_IF_USED(in.max_discharge_reactive_power, out.EVMaximumDischargeReactivePower);
    CPP2CB_CONVERT_IF_USED(in.max_discharge_reactive_power_L2, out.EVMaximumDischargeReactivePower_L2);
    CPP2CB_CONVERT_IF_USED(in.max_discharge_reactive_power_L3, out.EVMaximumDischargeReactivePower_L3);

    out.GridEventCondition = in.grid_event_condition;

    CPP2CB_CONVERT_IF_USED(in.max_v2x_energy_request, out.EVMaximumV2XEnergyRequest);
    CPP2CB_CONVERT_IF_USED(in.min_v2x_energy_request, out.EVMinimumV2XEnergyRequest);
    CPP2CB_CONVERT_IF_USED(in.session_total_discharge_energy_available, out.EVSessionTotalDischargeEnergyAvailable);
}

template <>
void convert(const struct iso20_ac_der_iec_DER_Dynamic_AC_CLReqControlModeType& in,
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

    convert(in.EVMaximumDischargePower, out.max_discharge_power);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumDischargePower_L2, out.max_discharge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumDischargePower_L3, out.max_discharge_power_L3);
    convert(in.EVMinimumDischargePower, out.min_discharge_power);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumDischargePower_L2, out.min_discharge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumDischargePower_L3, out.min_discharge_power_L3);

    CB2CPP_CONVERT_IF_USED(in.EVMaximumChargeReactivePower, out.max_charge_reactive_power);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumChargeReactivePower_L2, out.max_charge_reactive_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumChargeReactivePower_L3, out.max_charge_reactive_power_L3);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumDischargeReactivePower, out.max_discharge_reactive_power);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumDischargeReactivePower_L2, out.max_discharge_reactive_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumDischargeReactivePower_L3, out.max_discharge_reactive_power_L3);

    out.grid_event_condition = in.GridEventCondition;

    CB2CPP_CONVERT_IF_USED(in.EVMaximumV2XEnergyRequest, out.max_v2x_energy_request);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumV2XEnergyRequest, out.min_v2x_energy_request);
    CB2CPP_CONVERT_IF_USED(in.EVSessionTotalDischargeEnergyAvailable, out.session_total_discharge_energy_available);
}

template <>
void convert(const datatypes::DER_Scheduled_AC_CLReqControlMode& in,
             struct iso20_ac_der_iec_DER_Scheduled_AC_CLReqControlModeType& out) {

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

    convert(in.max_discharge_power, out.EVMaximumDischargePower);
    CPP2CB_CONVERT_IF_USED(in.max_discharge_power_L2, out.EVMaximumDischargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.max_discharge_power_L3, out.EVMaximumDischargePower_L3);
    convert(in.min_discharge_power, out.EVMinimumDischargePower);
    CPP2CB_CONVERT_IF_USED(in.min_discharge_power_L2, out.EVMinimumDischargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.min_discharge_power_L3, out.EVMinimumDischargePower_L3);

    CPP2CB_CONVERT_IF_USED(in.max_charge_reactive_power, out.EVMaximumChargeReactivePower);
    CPP2CB_CONVERT_IF_USED(in.max_charge_reactive_power_L2, out.EVMaximumChargeReactivePower_L2);
    CPP2CB_CONVERT_IF_USED(in.max_charge_reactive_power_L3, out.EVMaximumChargeReactivePower_L3);
    CPP2CB_CONVERT_IF_USED(in.max_discharge_reactive_power, out.EVMaximumDischargeReactivePower);
    CPP2CB_CONVERT_IF_USED(in.max_discharge_reactive_power_L2, out.EVMaximumDischargeReactivePower_L2);
    CPP2CB_CONVERT_IF_USED(in.max_discharge_reactive_power_L3, out.EVMaximumDischargeReactivePower_L3);

    out.GridEventCondition = in.grid_event_condition;
}

template <>
void convert(const struct iso20_ac_der_iec_DER_Scheduled_AC_CLReqControlModeType& in,
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

    convert(in.EVMaximumDischargePower, out.max_discharge_power);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumDischargePower_L2, out.max_discharge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumDischargePower_L3, out.max_discharge_power_L3);
    convert(in.EVMinimumDischargePower, out.min_discharge_power);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumDischargePower_L2, out.min_discharge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMinimumDischargePower_L3, out.min_discharge_power_L3);

    CB2CPP_CONVERT_IF_USED(in.EVMaximumChargeReactivePower, out.max_charge_reactive_power);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumChargeReactivePower_L2, out.max_charge_reactive_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumChargeReactivePower_L3, out.max_charge_reactive_power_L3);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumDischargeReactivePower, out.max_discharge_reactive_power);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumDischargeReactivePower_L2, out.max_discharge_reactive_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumDischargeReactivePower_L3, out.max_discharge_reactive_power_L3);

    out.grid_event_condition = in.GridEventCondition;
}

struct ReqControlModeVisitor {
    using DER_ScheduledCM = datatypes::DER_Scheduled_AC_CLReqControlMode;
    using DER_DynamicCM = datatypes::DER_Dynamic_AC_CLReqControlMode;

    ReqControlModeVisitor(iso20_ac_der_iec_AC_ChargeLoopReqType& req_) : req(req_) {};

    void operator()(const DER_ScheduledCM& in) {
        auto& out = req.DER_Scheduled_AC_CLReqControlMode;
        init_iso20_ac_der_iec_DER_Scheduled_AC_CLReqControlModeType(&out);
        convert(in, out);
        CB_SET_USED(req.DER_Scheduled_AC_CLReqControlMode);
    }

    void operator()(const DER_DynamicCM& in) {
        auto& out = req.DER_Dynamic_AC_CLReqControlMode;
        init_iso20_ac_der_iec_DER_Dynamic_AC_CLReqControlModeType(&out);
        convert(in, out);
        CB_SET_USED(req.DER_Dynamic_AC_CLReqControlMode);
    }

private:
    iso20_ac_der_iec_AC_ChargeLoopReqType& req;
};

template <> void convert(const DER_AC_ChargeLoopRequest& in, struct iso20_ac_der_iec_AC_ChargeLoopReqType& out) {
    init_iso20_ac_der_iec_AC_ChargeLoopReqType(&out);

    convert(in.header, out.Header);

    CPP2CB_CONVERT_IF_USED(in.display_parameters, out.DisplayParameters);
    out.MeterInfoRequested = in.meter_info_requested;

    std::visit(ReqControlModeVisitor(out), in.control_mode);
}

template <> void convert(const struct iso20_ac_der_iec_AC_ChargeLoopReqType& in, DER_AC_ChargeLoopRequest& out) {
    convert(in.Header, out.header);

    CB2CPP_CONVERT_IF_USED(in.DisplayParameters, out.display_parameters);
    out.meter_info_requested = in.MeterInfoRequested;

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

template <> int serialize_to_exi(const DER_AC_ChargeLoopRequest& in, exi_bitstream_t& out) {
    iso20_ac_der_iec_exiDocument doc{};
    init_iso20_ac_der_iec_exiDocument(&doc);

    CB_SET_USED(doc.AC_ChargeLoopReq);

    convert(in, doc.AC_ChargeLoopReq);

    return encode_iso20_ac_der_iec_exiDocument(&out, &doc);
}

template <> size_t serialize(const DER_AC_ChargeLoopRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> void insert_type(VariantAccess& va, const struct iso20_ac_der_iec_AC_ChargeLoopReqType& in) {
    va.insert_type<DER_AC_ChargeLoopRequest>(in);
}

// Response
template <> void convert(const datatypes::DetailedCost& in, struct iso20_ac_der_iec_DetailedCostType& out) {
    init_iso20_ac_der_iec_DetailedCostType(&out);
    convert(in.amount, out.Amount);
    convert(in.cost_per_unit, out.CostPerUnit);
}

template <> void convert(const struct iso20_ac_der_iec_DetailedCostType& in, datatypes::DetailedCost& out) {
    convert(in.Amount, out.amount);
    convert(in.CostPerUnit, out.cost_per_unit);
}

template <> void convert(const datatypes::DetailedTax& in, struct iso20_ac_der_iec_DetailedTaxType& out) {
    init_iso20_ac_der_iec_DetailedTaxType(&out);
    out.TaxRuleID = in.tax_rule_id;
    convert(in.amount, out.Amount);
}

template <> void convert(const struct iso20_ac_der_iec_DetailedTaxType& in, datatypes::DetailedTax& out) {
    out.tax_rule_id = in.TaxRuleID;
    convert(in.Amount, out.amount);
}

template <> void convert(const datatypes::Receipt& in, struct iso20_ac_der_iec_ReceiptType& out) {
    init_iso20_ac_der_iec_ReceiptType(&out);

    out.TimeAnchor = in.time_anchor;
    CPP2CB_CONVERT_IF_USED(in.energy_costs, out.EnergyCosts);
    CPP2CB_CONVERT_IF_USED(in.occupancy_costs, out.OccupancyCosts);
    CPP2CB_CONVERT_IF_USED(in.additional_service_costs, out.AdditionalServicesCosts);
    CPP2CB_CONVERT_IF_USED(in.overstay_costs, out.OverstayCosts);

    if (sizeof(out.TaxCosts.array) < in.tax_costs.size()) {
        throw std::runtime_error("tax costs array is too large");
    }
    for (std::size_t i = 0; i < in.tax_costs.size(); ++i) {
        convert(in.tax_costs[i], out.TaxCosts.array[i]);
    }
    out.TaxCosts.arrayLen = in.tax_costs.size();
}

template <> void convert(const struct iso20_ac_der_iec_ReceiptType& in, datatypes::Receipt& out) {
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

template <> void convert(const datatypes::DsoQSetpoint& in, struct iso20_ac_der_iec_DSOQSetpointType& out) {
    convert(in.dso_q_setpoint_value, out.DSOQSetpointValue);
    CPP2CB_CONVERT_IF_USED(in.dso_q_setpoint_value_L2, out.DSOQSetpointValue_L2);
    CPP2CB_CONVERT_IF_USED(in.dso_q_setpoint_value_L3, out.DSOQSetpointValue_L3);
    out.PT1ResponseReactivePower = in.pt1_response_reactive_power;
    convert(in.step_response_time_constant_reactive_power, out.StepResponseTimeConstantReactivePower);
}

template <> void convert(const struct iso20_ac_der_iec_DSOQSetpointType& in, datatypes::DsoQSetpoint& out) {
    convert(in.DSOQSetpointValue, out.dso_q_setpoint_value);
    CB2CPP_CONVERT_IF_USED(in.DSOQSetpointValue_L2, out.dso_q_setpoint_value_L2);
    CB2CPP_CONVERT_IF_USED(in.DSOQSetpointValue_L3, out.dso_q_setpoint_value_L3);
    out.pt1_response_reactive_power = in.PT1ResponseReactivePower;
    convert(in.StepResponseTimeConstantReactivePower, out.step_response_time_constant_reactive_power);
}

template <> void convert(const datatypes::DsoCosPhiSetpoint& in, struct iso20_ac_der_iec_DSOCosPhiSetpointType& out) {
    convert(in.dso_cos_phi_setpoint_value, out.DSOCosPhiSetpointValue);
    CPP2CB_CONVERT_IF_USED(in.dso_cos_phi_setpoint_value_L2, out.DSOCosPhiSetpointValue_L2);
    CPP2CB_CONVERT_IF_USED(in.dso_cos_phi_setpoint_value_L3, out.DSOCosPhiSetpointValue_L3);
    cb_convert_enum(in.excitation, out.Excitation);
    out.PT1ResponseReactivePower = in.pt1_response_reactive_power;
    convert(in.step_response_time_constant_reactive_power, out.StepResponseTimeConstantReactivePower);
}

template <> void convert(const struct iso20_ac_der_iec_DSOCosPhiSetpointType& in, datatypes::DsoCosPhiSetpoint& out) {
    convert(in.DSOCosPhiSetpointValue, out.dso_cos_phi_setpoint_value);
    CB2CPP_CONVERT_IF_USED(in.DSOCosPhiSetpointValue_L2, out.dso_cos_phi_setpoint_value_L2);
    CB2CPP_CONVERT_IF_USED(in.DSOCosPhiSetpointValue_L3, out.dso_cos_phi_setpoint_value_L3);
    cb_convert_enum(in.Excitation, out.excitation);
    out.pt1_response_reactive_power = in.PT1ResponseReactivePower;
    convert(in.StepResponseTimeConstantReactivePower, out.step_response_time_constant_reactive_power);
}

template <>
void convert(const datatypes::DER_Scheduled_AC_CLResControlMode& in,
             struct iso20_ac_der_iec_DER_Scheduled_AC_CLResControlModeType& out) {
    CPP2CB_CONVERT_IF_USED(in.target_active_power, out.EVSETargetActivePower);
    CPP2CB_CONVERT_IF_USED(in.target_active_power_L2, out.EVSETargetActivePower_L2);
    CPP2CB_CONVERT_IF_USED(in.target_active_power_L2, out.EVSETargetActivePower_L3);

    CPP2CB_CONVERT_IF_USED(in.target_reactive_power, out.EVSETargetReactivePower);
    CPP2CB_CONVERT_IF_USED(in.target_reactive_power_L2, out.EVSETargetReactivePower_L2);
    CPP2CB_CONVERT_IF_USED(in.target_reactive_power_L2, out.EVSETargetReactivePower_L3);

    CPP2CB_CONVERT_IF_USED(in.present_active_power, out.EVSEPresentActivePower);
    CPP2CB_CONVERT_IF_USED(in.present_active_power_L2, out.EVSEPresentActivePower_L2);
    CPP2CB_CONVERT_IF_USED(in.present_active_power_L3, out.EVSEPresentActivePower_L3);
    convert(in.max_charge_power, out.EVSEMaximumChargePower);
    CPP2CB_CONVERT_IF_USED(in.max_charge_power_L2, out.EVSEMaximumChargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.max_charge_power_L3, out.EVSEMaximumChargePower_L3);

    convert(in.max_discharge_power, out.EVSEMaximumDischargePower);
    CPP2CB_CONVERT_IF_USED(in.max_discharge_power_L2, out.EVSEMaximumDischargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.max_discharge_power_L3, out.EVSEMaximumDischargePower_L3);

    CPP2CB_CONVERT_IF_USED(in.dso_discharge_power, out.DSOMaximumDischargePower);
    CPP2CB_CONVERT_IF_USED(in.dso_discharge_power_L2, out.DSOMaximumDischargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.dso_discharge_power_L3, out.DSOMaximumDischargePower_L3);

    CPP2CB_CONVERT_IF_USED(in.dso_q_setpoint, out.DSOQSetpoint);
    CPP2CB_CONVERT_IF_USED(in.dso_cos_phi_setpoint, out.DSOCosPhiSetpoint);
}

template <>
void convert(const struct iso20_ac_der_iec_DER_Scheduled_AC_CLResControlModeType& in,
             datatypes::DER_Scheduled_AC_CLResControlMode& out) {
    CB2CPP_CONVERT_IF_USED(in.EVSETargetActivePower, out.target_active_power);
    CB2CPP_CONVERT_IF_USED(in.EVSETargetActivePower_L2, out.target_active_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSETargetActivePower_L3, out.target_active_power_L3);

    CB2CPP_CONVERT_IF_USED(in.EVSETargetReactivePower, out.target_reactive_power);
    CB2CPP_CONVERT_IF_USED(in.EVSETargetReactivePower_L2, out.target_reactive_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSETargetReactivePower_L3, out.target_reactive_power_L3);

    CB2CPP_CONVERT_IF_USED(in.EVSEPresentActivePower, out.present_active_power);
    CB2CPP_CONVERT_IF_USED(in.EVSEPresentActivePower_L2, out.present_active_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSEPresentActivePower_L3, out.present_active_power_L3);
    convert(in.EVSEMaximumChargePower, out.max_charge_power);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumChargePower_L2, out.max_charge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumChargePower_L3, out.max_charge_power_L3);

    convert(in.EVSEMaximumDischargePower, out.max_discharge_power);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumDischargePower_L2, out.max_discharge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumDischargePower_L3, out.max_discharge_power_L3);

    CB2CPP_CONVERT_IF_USED(in.DSOMaximumDischargePower, out.dso_discharge_power);
    CB2CPP_CONVERT_IF_USED(in.DSOMaximumDischargePower_L2, out.dso_discharge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.DSOMaximumDischargePower_L3, out.dso_discharge_power_L3);

    CB2CPP_CONVERT_IF_USED(in.DSOQSetpoint, out.dso_q_setpoint);
    CB2CPP_CONVERT_IF_USED(in.DSOCosPhiSetpoint, out.dso_cos_phi_setpoint);
}

template <>
void convert(const datatypes::DER_Dynamic_AC_CLResControlMode& in,
             struct iso20_ac_der_iec_DER_Dynamic_AC_CLResControlModeType& out) {

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
    convert(in.max_charge_power, out.EVSEMaximumChargePower);
    CPP2CB_CONVERT_IF_USED(in.max_charge_power_L2, out.EVSEMaximumChargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.max_charge_power_L3, out.EVSEMaximumChargePower_L3);

    convert(in.max_discharge_power, out.EVSEMaximumDischargePower);
    CPP2CB_CONVERT_IF_USED(in.max_discharge_power_L2, out.EVSEMaximumDischargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.max_discharge_power_L3, out.EVSEMaximumDischargePower_L3);

    CPP2CB_CONVERT_IF_USED(in.dso_discharge_power, out.DSOMaximumDischargePower);
    CPP2CB_CONVERT_IF_USED(in.dso_discharge_power_L2, out.DSOMaximumDischargePower_L2);
    CPP2CB_CONVERT_IF_USED(in.dso_discharge_power_L3, out.DSOMaximumDischargePower_L3);

    CPP2CB_CONVERT_IF_USED(in.dso_q_setpoint, out.DSOQSetpoint);
    CPP2CB_CONVERT_IF_USED(in.dso_cos_phi_setpoint, out.DSOCosPhiSetpoint);
}

template <>
void convert(const struct iso20_ac_der_iec_DER_Dynamic_AC_CLResControlModeType& in,
             datatypes::DER_Dynamic_AC_CLResControlMode& out) {
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

    convert(in.EVSEMaximumChargePower, out.max_charge_power);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumChargePower_L2, out.max_charge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumChargePower_L3, out.max_charge_power_L3);

    convert(in.EVSEMaximumDischargePower, out.max_discharge_power);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumDischargePower_L2, out.max_discharge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.EVSEMaximumDischargePower_L3, out.max_discharge_power_L3);

    CB2CPP_CONVERT_IF_USED(in.DSOMaximumDischargePower, out.dso_discharge_power);
    CB2CPP_CONVERT_IF_USED(in.DSOMaximumDischargePower_L2, out.dso_discharge_power_L2);
    CB2CPP_CONVERT_IF_USED(in.DSOMaximumDischargePower_L3, out.dso_discharge_power_L3);

    CB2CPP_CONVERT_IF_USED(in.DSOQSetpoint, out.dso_q_setpoint);
    CB2CPP_CONVERT_IF_USED(in.DSOCosPhiSetpoint, out.dso_cos_phi_setpoint);
}

struct ControlModeVisitor {
    using DER_ScheduledCM = datatypes::DER_Scheduled_AC_CLResControlMode;
    using DER_DynamicCM = datatypes::DER_Dynamic_AC_CLResControlMode;

    ControlModeVisitor(iso20_ac_der_iec_AC_ChargeLoopResType& res_) : res(res_) {};

    void operator()(const DER_ScheduledCM& in) {
        auto& out = res.DER_Scheduled_AC_CLResControlMode;
        init_iso20_ac_der_iec_DER_Scheduled_AC_CLResControlModeType(&out);
        convert(in, out);
        CB_SET_USED(res.DER_Scheduled_AC_CLResControlMode);
    }

    void operator()(const DER_DynamicCM& in) {
        auto& out = res.DER_Dynamic_AC_CLResControlMode;
        init_iso20_ac_der_iec_DER_Dynamic_AC_CLResControlModeType(&out);
        convert(in, out);
        CB_SET_USED(res.DER_Dynamic_AC_CLResControlMode);
    }

private:
    iso20_ac_der_iec_AC_ChargeLoopResType& res;
};

template <> void convert(const DER_AC_ChargeLoopResponse& in, struct iso20_ac_der_iec_AC_ChargeLoopResType& out) {
    init_iso20_ac_der_iec_AC_ChargeLoopResType(&out);

    convert(in.header, out.Header);
    cb_convert_enum(in.response_code, out.ResponseCode);

    CPP2CB_CONVERT_IF_USED(in.status, out.EVSEStatus);
    CPP2CB_CONVERT_IF_USED(in.meter_info, out.MeterInfo);
    CPP2CB_CONVERT_IF_USED(in.receipt, out.Receipt);

    CPP2CB_CONVERT_IF_USED(in.target_frequency, out.EVSETargetFrequency);

    std::visit(ControlModeVisitor(out), in.control_mode);
}

template <> void convert(const struct iso20_ac_der_iec_AC_ChargeLoopResType& in, DER_AC_ChargeLoopResponse& out) {
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

template <> int serialize_to_exi(const DER_AC_ChargeLoopResponse& in, exi_bitstream_t& out) {
    iso20_ac_der_iec_exiDocument doc{};
    init_iso20_ac_der_iec_exiDocument(&doc);

    CB_SET_USED(doc.AC_ChargeLoopRes);

    convert(in, doc.AC_ChargeLoopRes);

    return encode_iso20_ac_der_iec_exiDocument(&out, &doc);
}

template <> size_t serialize(const DER_AC_ChargeLoopResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> void insert_type(VariantAccess& va, const struct iso20_ac_der_iec_AC_ChargeLoopResType& in) {
    va.insert_type<DER_AC_ChargeLoopResponse>(in);
}

} // namespace iso15118::message_20
