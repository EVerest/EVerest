// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <cstddef>
#include <iso15118/message/din/charge_parameter_discovery.hpp>

#include <iso15118/detail/variant_access.hpp>

#include <cbv2g/din/din_msgDefDecoder.h>
#include <cbv2g/din/din_msgDefEncoder.h>

#include <iso15118/detail/helper.hpp>
#include <variant>

namespace iso15118::din::msg {

template <> void convert(const struct din_AC_EVChargeParameterType& in, data_types::AcEvChargeParameter& out) {
    out.departure_time = in.DepartureTime;
    convert(in.EAmount, out.e_amount);
    convert(in.EVMaxVoltage, out.ev_max_voltage);
    convert(in.EVMaxCurrent, out.ev_max_current);
    convert(in.EVMinCurrent, out.ev_min_current);
}

template <> void convert(const struct din_DC_EVChargeParameterType& in, data_types::DcEvChargeParameter& out) {
    convert(in.DC_EVStatus, out.dc_ev_status);
    convert(in.EVMaximumCurrentLimit, out.ev_maximum_current_limit);
    CB2CPP_CONVERT_IF_USED(in.EVMaximumPowerLimit, out.ev_maximum_power_limit);
    convert(in.EVMaximumVoltageLimit, out.ev_maximum_voltage_limit);
    CB2CPP_CONVERT_IF_USED(in.EVEnergyCapacity, out.ev_energy_capacity);
    CB2CPP_CONVERT_IF_USED(in.EVEnergyRequest, out.ev_energy_request);
    CB2CPP_ASSIGN_IF_USED(in.FullSOC, out.full_soc);
    CB2CPP_ASSIGN_IF_USED(in.BulkSOC, out.bulk_soc);
}

template <> void convert(const data_types::AcEvseChargeParameter& in, struct din_AC_EVSEChargeParameterType& out) {
    init_din_AC_EVSEChargeParameterType(&out);
    convert(in.ac_evse_status, out.AC_EVSEStatus);
    convert(in.evse_max_voltage, out.EVSEMaxVoltage);
    convert(in.evse_max_current, out.EVSEMaxCurrent);
    convert(in.evse_min_current, out.EVSEMinCurrent);
}

template <> void convert(const data_types::DcEvseChargeParameter& in, struct din_DC_EVSEChargeParameterType& out) {
    init_din_DC_EVSEChargeParameterType(&out);
    convert(in.dc_evse_status, out.DC_EVSEStatus);
    convert(in.evse_maximum_current_limit, out.EVSEMaximumCurrentLimit);
    CPP2CB_CONVERT_IF_USED(in.evse_maximum_power_limit, out.EVSEMaximumPowerLimit);
    convert(in.evse_maximum_voltage_limit, out.EVSEMaximumVoltageLimit);
    convert(in.evse_minimum_current_limit, out.EVSEMinimumCurrentLimit);
    convert(in.evse_minimum_voltage_limit, out.EVSEMinimumVoltageLimit);
    CPP2CB_CONVERT_IF_USED(in.evse_current_regulation_tolerance, out.EVSECurrentRegulationTolerance);
    convert(in.evse_peak_current_ripple, out.EVSEPeakCurrentRipple);
    CPP2CB_CONVERT_IF_USED(in.evse_energy_to_be_delivered, out.EVSEEnergyToBeDelivered);
}

template <> void convert(const data_types::RelativeTimeInterval& in, struct din_RelativeTimeIntervalType& out) {
    init_din_RelativeTimeIntervalType(&out);
    out.start = in.start;
    CPP2CB_ASSIGN_IF_USED(in.duration, out.duration);
}

template <> void convert(const data_types::PMaxScheduleEntry& in, struct din_PMaxScheduleEntryType& out) {
    init_din_PMaxScheduleEntryType(&out);
    convert(in.time_interval, out.RelativeTimeInterval);
    CB_SET_USED(out.RelativeTimeInterval);
    out.PMax = in.p_max;
}

template <> void convert(const data_types::PMaxSchedule& in, struct din_PMaxScheduleType& out) {
    init_din_PMaxScheduleType(&out);
    out.PMaxScheduleID = in.pmax_schedule_id;
    for (size_t i = 0; i < in.pmax_schedule_entry.size(); i++) {
        const auto& entry_in = in.pmax_schedule_entry.at(i);
        auto& entry_out = out.PMaxScheduleEntry.array[i];
        convert(entry_in, entry_out);
    }
    out.PMaxScheduleEntry.arrayLen = in.pmax_schedule_entry.size();
}

template <> void convert(const data_types::ConsumptionCost& in, struct din_ConsumptionCostType& out) {
    init_din_ConsumptionCostType(&out);
    out.startValue = in.start_value;

    if (in.cost.has_value()) {
        init_din_CostType(&out.Cost);
        cb_convert_enum(in.cost->cost_kind, out.Cost.costKind);
        out.Cost.amount = in.cost->amount;
        CPP2CB_ASSIGN_IF_USED(in.cost->amount_multiplier, out.Cost.amountMultiplier);
        CB_SET_USED(out.Cost);
    }
}

template <> void convert(const data_types::SalesTariffEntry& in, struct din_SalesTariffEntryType& out) {
    init_din_SalesTariffEntryType(&out);
    convert(in.time_interval, out.RelativeTimeInterval);
    CB_SET_USED(out.RelativeTimeInterval);
    out.EPriceLevel = in.e_price_level;

    if (in.consumption_cost.has_value()) {
        convert(in.consumption_cost.value(), out.ConsumptionCost);
        CB_SET_USED(out.ConsumptionCost);
    }
}

template <> void convert(const data_types::SalesTariff& in, struct din_SalesTariffType& out) {
    init_din_SalesTariffType(&out);
    CPP2CB_STRING(in.id, out.Id);
    out.SalesTariffID = in.sales_tariff_id;
    CPP2CB_STRING_IF_USED(in.sales_tariff_description, out.SalesTariffDescription);
    out.NumEPriceLevels = in.num_e_price_levels;

    for (size_t i = 0; i < in.sales_tariff_entry.size(); i++) {
        const auto& entry_in = in.sales_tariff_entry.at(i);
        auto& entry_out = out.SalesTariffEntry.array[i];
        convert(entry_in, entry_out);
    }
    out.SalesTariffEntry.arrayLen = in.sales_tariff_entry.size();
}

template <> void convert(const data_types::SaScheduleTuple& in, struct din_SAScheduleTupleType& out) {
    init_din_SAScheduleTupleType(&out);
    out.SAScheduleTupleID = in.sa_schedule_tuple_id;

    convert(in.pmax_schedule, out.PMaxSchedule);

    if (in.sales_tariff.has_value()) {
        convert(in.sales_tariff.value(), out.SalesTariff);
        CB_SET_USED(out.SalesTariff);
    }
}

template <> void convert(const struct din_ChargeParameterDiscoveryReqType& in, ChargeParameterDiscoveryRequest& out) {
    cb_convert_enum(in.EVRequestedEnergyTransferType, out.requested_energy_transfer_mode);
    if (in.AC_EVChargeParameter_isUsed) {
        data_types::AcEvChargeParameter param;
        convert(in.AC_EVChargeParameter, param);
        out.ev_charge_parameter = param;
    } else if (in.DC_EVChargeParameter_isUsed) {
        data_types::DcEvChargeParameter param;
        convert(in.DC_EVChargeParameter, param);
        out.ev_charge_parameter = param;
    }
}

template <>
void insert_type(VariantAccess& va, const struct din_ChargeParameterDiscoveryReqType& in,
                 const struct din_MessageHeaderType& header) {
    va.insert_type<ChargeParameterDiscoveryRequest>(in, header);
}

template <> void convert(const ChargeParameterDiscoveryResponse& in, struct din_ChargeParameterDiscoveryResType& out) {
    init_din_ChargeParameterDiscoveryResType(&out);

    cb_convert_enum(in.response_code, out.ResponseCode);
    cb_convert_enum(in.evse_processing, out.EVSEProcessing);

    if (in.sa_schedule_list.has_value()) {
        init_din_SAScheduleListType(&out.SAScheduleList);
        for (size_t i = 0; i < in.sa_schedule_list->size(); i++) {
            const auto& schedule_in = in.sa_schedule_list->at(i);
            auto& schedule_out = out.SAScheduleList.SAScheduleTuple.array[i];
            convert(schedule_in, schedule_out);
        }
        out.SAScheduleList.SAScheduleTuple.arrayLen = in.sa_schedule_list->size();
        CB_SET_USED(out.SAScheduleList);
    }

    if (std::holds_alternative<data_types::AcEvseChargeParameter>(in.evse_charge_parameter)) {
        const auto& param = std::get<data_types::AcEvseChargeParameter>(in.evse_charge_parameter);
        convert(param, out.AC_EVSEChargeParameter);
        CB_SET_USED(out.AC_EVSEChargeParameter);
    } else if (std::holds_alternative<data_types::DcEvseChargeParameter>(in.evse_charge_parameter)) {
        const auto& param = std::get<data_types::DcEvseChargeParameter>(in.evse_charge_parameter);
        convert(param, out.DC_EVSEChargeParameter);
        CB_SET_USED(out.DC_EVSEChargeParameter);
    }
}

template <> int serialize_to_exi(const ChargeParameterDiscoveryResponse& in, exi_bitstream_t& out) {

    din_exiDocument doc;
    init_din_exiDocument(&doc);
    init_din_BodyType(&doc.V2G_Message.Body);

    convert(in.header, doc.V2G_Message.Header);

    CB_SET_USED(doc.V2G_Message.Body.ChargeParameterDiscoveryRes);
    convert(in, doc.V2G_Message.Body.ChargeParameterDiscoveryRes);

    return encode_din_exiDocument(&out, &doc);
}

template <> size_t serialize(const ChargeParameterDiscoveryResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::din::msg
