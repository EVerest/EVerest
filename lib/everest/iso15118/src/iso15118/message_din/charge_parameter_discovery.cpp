// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_din/charge_parameter_discovery.hpp>

#include <iso15118/detail/message_din/variant_access.hpp>

#include <cbv2g/din/din_msgDefDatatypes.h>
#include <cbv2g/din/din_msgDefEncoder.h>

namespace iso15118::message_din {

using datatypes::Unit;

static void convert(const struct din_DC_EVChargeParameterType& in, datatypes::DcEvChargeParameter& out) {
    convert(in.DC_EVStatus, out.dc_ev_status);
    out.ev_maximum_current_limit = from_physical_value(in.EVMaximumCurrentLimit);
    if (in.EVMaximumPowerLimit_isUsed) {
        out.ev_maximum_power_limit = from_physical_value(in.EVMaximumPowerLimit);
    }
    out.ev_maximum_voltage_limit = from_physical_value(in.EVMaximumVoltageLimit);
    if (in.EVEnergyCapacity_isUsed) {
        out.ev_energy_capacity = from_physical_value(in.EVEnergyCapacity);
    }
    if (in.EVEnergyRequest_isUsed) {
        out.ev_energy_request = from_physical_value(in.EVEnergyRequest);
    }
    CB2CPP_ASSIGN_IF_USED(in.FullSOC, out.full_soc);
    CB2CPP_ASSIGN_IF_USED(in.BulkSOC, out.bulk_soc);
}

static void convert(const datatypes::DcEvChargeParameter& in, struct din_DC_EVChargeParameterType& out) {
    init_din_DC_EVChargeParameterType(&out);
    convert(in.dc_ev_status, out.DC_EVStatus);
    out.EVMaximumCurrentLimit = to_physical_value(in.ev_maximum_current_limit, Unit::A);
    if (in.ev_maximum_power_limit) {
        out.EVMaximumPowerLimit = to_physical_value(in.ev_maximum_power_limit.value(), Unit::W);
        CB_SET_USED(out.EVMaximumPowerLimit);
    }
    out.EVMaximumVoltageLimit = to_physical_value(in.ev_maximum_voltage_limit, Unit::V);
    if (in.ev_energy_capacity) {
        out.EVEnergyCapacity = to_physical_value(in.ev_energy_capacity.value(), Unit::Wh);
        CB_SET_USED(out.EVEnergyCapacity);
    }
    if (in.ev_energy_request) {
        out.EVEnergyRequest = to_physical_value(in.ev_energy_request.value(), Unit::Wh);
        CB_SET_USED(out.EVEnergyRequest);
    }
    CPP2CB_ASSIGN_IF_USED(in.full_soc, out.FullSOC);
    CPP2CB_ASSIGN_IF_USED(in.bulk_soc, out.BulkSOC);
}

static void convert(const struct din_DC_EVSEChargeParameterType& in, datatypes::DcEvseChargeParameter& out) {
    convert(in.DC_EVSEStatus, out.dc_evse_status);
    out.evse_maximum_current_limit = from_physical_value(in.EVSEMaximumCurrentLimit);
    if (in.EVSEMaximumPowerLimit_isUsed) {
        out.evse_maximum_power_limit = from_physical_value(in.EVSEMaximumPowerLimit);
    }
    out.evse_maximum_voltage_limit = from_physical_value(in.EVSEMaximumVoltageLimit);
    out.evse_minimum_current_limit = from_physical_value(in.EVSEMinimumCurrentLimit);
    out.evse_minimum_voltage_limit = from_physical_value(in.EVSEMinimumVoltageLimit);
    if (in.EVSECurrentRegulationTolerance_isUsed) {
        out.evse_current_regulation_tolerance = from_physical_value(in.EVSECurrentRegulationTolerance);
    }
    out.evse_peak_current_ripple = from_physical_value(in.EVSEPeakCurrentRipple);
    if (in.EVSEEnergyToBeDelivered_isUsed) {
        out.evse_energy_to_be_delivered = from_physical_value(in.EVSEEnergyToBeDelivered);
    }
}

static void convert(const datatypes::DcEvseChargeParameter& in, struct din_DC_EVSEChargeParameterType& out) {
    init_din_DC_EVSEChargeParameterType(&out);
    convert(in.dc_evse_status, out.DC_EVSEStatus);
    out.EVSEMaximumCurrentLimit = to_physical_value(in.evse_maximum_current_limit, Unit::A);
    if (in.evse_maximum_power_limit) {
        out.EVSEMaximumPowerLimit = to_physical_value(in.evse_maximum_power_limit.value(), Unit::W);
        CB_SET_USED(out.EVSEMaximumPowerLimit);
    }
    out.EVSEMaximumVoltageLimit = to_physical_value(in.evse_maximum_voltage_limit, Unit::V);
    out.EVSEMinimumCurrentLimit = to_physical_value(in.evse_minimum_current_limit, Unit::A);
    out.EVSEMinimumVoltageLimit = to_physical_value(in.evse_minimum_voltage_limit, Unit::V);
    if (in.evse_current_regulation_tolerance) {
        out.EVSECurrentRegulationTolerance =
            to_physical_value(in.evse_current_regulation_tolerance.value(), Unit::A);
        CB_SET_USED(out.EVSECurrentRegulationTolerance);
    }
    out.EVSEPeakCurrentRipple = to_physical_value(in.evse_peak_current_ripple, Unit::A);
    if (in.evse_energy_to_be_delivered) {
        out.EVSEEnergyToBeDelivered = to_physical_value(in.evse_energy_to_be_delivered.value(), Unit::Wh);
        CB_SET_USED(out.EVSEEnergyToBeDelivered);
    }
}

template <> void convert(const struct din_ChargeParameterDiscoveryReqType& in, ChargeParameterDiscoveryRequest& out) {
    cb_convert_enum(in.EVRequestedEnergyTransferType, out.ev_requested_energy_transfer_type);
    if (in.DC_EVChargeParameter_isUsed) {
        convert(in.DC_EVChargeParameter, out.dc_ev_charge_parameter.emplace());
    }
}

template <> void convert(const ChargeParameterDiscoveryRequest& in, struct din_ChargeParameterDiscoveryReqType& out) {
    init_din_ChargeParameterDiscoveryReqType(&out);
    cb_convert_enum(in.ev_requested_energy_transfer_type, out.EVRequestedEnergyTransferType);
    if (in.dc_ev_charge_parameter) {
        convert(in.dc_ev_charge_parameter.value(), out.DC_EVChargeParameter);
        CB_SET_USED(out.DC_EVChargeParameter);
    }
}

static void convert(const struct din_SAScheduleListType& in, datatypes::SAScheduleList& out) {
    for (uint16_t t = 0; t < in.SAScheduleTuple.arrayLen and t < din_SAScheduleTupleType_5_ARRAY_SIZE; ++t) {
        const auto& in_tuple = in.SAScheduleTuple.array[t];
        auto& out_tuple = out.emplace_back();
        out_tuple.sa_schedule_tuple_id = in_tuple.SAScheduleTupleID;
        out_tuple.pmax_schedule_id = in_tuple.PMaxSchedule.PMaxScheduleID;
        for (uint16_t e = 0; e < in_tuple.PMaxSchedule.PMaxScheduleEntry.arrayLen and
                             e < din_PMaxScheduleEntryType_5_ARRAY_SIZE;
             ++e) {
            const auto& in_entry = in_tuple.PMaxSchedule.PMaxScheduleEntry.array[e];
            auto& out_entry = out_tuple.pmax_schedule.emplace_back();
            out_entry.start = in_entry.RelativeTimeInterval.start;
            out_entry.duration = in_entry.RelativeTimeInterval.duration;
            out_entry.p_max = in_entry.PMax;
        }
    }
}

static void convert(const datatypes::SAScheduleList& in, struct din_SAScheduleListType& out) {
    init_din_SAScheduleListType(&out);
    uint16_t tuple_count = 0;
    for (const auto& in_tuple : in) {
        if (tuple_count >= din_SAScheduleTupleType_5_ARRAY_SIZE) {
            break;
        }
        auto& out_tuple = out.SAScheduleTuple.array[tuple_count];
        init_din_SAScheduleTupleType(&out_tuple);
        out_tuple.SAScheduleTupleID = in_tuple.sa_schedule_tuple_id;
        out_tuple.PMaxSchedule.PMaxScheduleID = in_tuple.pmax_schedule_id;
        // [V2G-DC-554] SalesTariff shall not be used in DIN 70121.
        out_tuple.SalesTariff_isUsed = 0;

        uint16_t entry_count = 0;
        for (const auto& in_entry : in_tuple.pmax_schedule) {
            if (entry_count >= din_PMaxScheduleEntryType_5_ARRAY_SIZE) {
                break;
            }
            auto& out_entry = out_tuple.PMaxSchedule.PMaxScheduleEntry.array[entry_count];
            init_din_PMaxScheduleEntryType(&out_entry);
            out_entry.PMax = in_entry.p_max;
            out_entry.RelativeTimeInterval.start = in_entry.start;
            out_entry.RelativeTimeInterval.duration = in_entry.duration;
            out_entry.RelativeTimeInterval.duration_isUsed = 1; // Must be used in DIN
            CB_SET_USED(out_entry.RelativeTimeInterval);
            out_entry.TimeInterval_isUsed = 0;
            ++entry_count;
        }
        out_tuple.PMaxSchedule.PMaxScheduleEntry.arrayLen = entry_count;
        ++tuple_count;
    }
    out.SAScheduleTuple.arrayLen = tuple_count;
}

template <> void convert(const struct din_ChargeParameterDiscoveryResType& in, ChargeParameterDiscoveryResponse& out) {
    cb_convert_enum(in.ResponseCode, out.response_code);
    cb_convert_enum(in.EVSEProcessing, out.evse_processing);
    if (in.SAScheduleList_isUsed) {
        convert(in.SAScheduleList, out.sa_schedule_list.emplace());
    }
    if (in.DC_EVSEChargeParameter_isUsed) {
        convert(in.DC_EVSEChargeParameter, out.dc_evse_charge_parameter.emplace());
    }
}

template <> void convert(const ChargeParameterDiscoveryResponse& in, struct din_ChargeParameterDiscoveryResType& out) {
    init_din_ChargeParameterDiscoveryResType(&out);
    cb_convert_enum(in.response_code, out.ResponseCode);
    cb_convert_enum(in.evse_processing, out.EVSEProcessing);
    if (in.sa_schedule_list) {
        convert(in.sa_schedule_list.value(), out.SAScheduleList);
        CB_SET_USED(out.SAScheduleList);
    }
    if (in.dc_evse_charge_parameter) {
        convert(in.dc_evse_charge_parameter.value(), out.DC_EVSEChargeParameter);
        CB_SET_USED(out.DC_EVSEChargeParameter);
    }
}

template <> void insert_type(VariantAccess& va, const struct din_ChargeParameterDiscoveryReqType& in) {
    va.insert_type<ChargeParameterDiscoveryRequest>(in);
}

template <> void insert_type(VariantAccess& va, const struct din_ChargeParameterDiscoveryResType& in) {
    va.insert_type<ChargeParameterDiscoveryResponse>(in);
}

template <> int serialize_to_exi(const ChargeParameterDiscoveryRequest& in, exi_bitstream_t& out) {
    din_exiDocument doc;
    init_din_exiDocument(&doc);
    init_din_BodyType(&doc.V2G_Message.Body);
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.ChargeParameterDiscoveryReq);
    convert(in, doc.V2G_Message.Body.ChargeParameterDiscoveryReq);
    return encode_din_exiDocument(&out, &doc);
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

template <> size_t serialize(const ChargeParameterDiscoveryRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> size_t serialize(const ChargeParameterDiscoveryResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_din
