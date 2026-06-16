// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/message/ac_der_sae_charge_loop.hpp>

#include <iso15118/detail/variant_access.hpp>

#include <cbv2g/iso_20/iso20_AC_DER_SAE_Decoder.h>
#include <cbv2g/iso_20/iso20_AC_DER_SAE_Encoder.h>

namespace iso15118::message_20 {

namespace dts = datatypes::sae;

// Response

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

    if (sizeof(out.TaxCosts.array) < in.tax_costs.size()) {
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
