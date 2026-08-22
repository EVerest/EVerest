// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_2/current_demand.hpp>

#include <iso15118/detail/message_2/variant_access.hpp>

#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>
#include <cbv2g/iso_2/iso2_msgDefEncoder.h>

namespace iso15118::message_2 {

template <> void convert(const struct iso2_CurrentDemandReqType& in, CurrentDemandRequest& out) {
    convert(in.DC_EVStatus, out.dc_ev_status);
    convert(in.EVTargetCurrent, out.ev_target_current);
    if (in.EVMaximumVoltageLimit_isUsed) {
        convert(in.EVMaximumVoltageLimit, out.ev_maximum_voltage_limit.emplace());
    }
    if (in.EVMaximumCurrentLimit_isUsed) {
        convert(in.EVMaximumCurrentLimit, out.ev_maximum_current_limit.emplace());
    }
    if (in.EVMaximumPowerLimit_isUsed) {
        convert(in.EVMaximumPowerLimit, out.ev_maximum_power_limit.emplace());
    }
    if (in.BulkChargingComplete_isUsed) {
        out.bulk_charging_complete = static_cast<bool>(in.BulkChargingComplete);
    }
    out.charging_complete = in.ChargingComplete;
    if (in.RemainingTimeToFullSoC_isUsed) {
        convert(in.RemainingTimeToFullSoC, out.remaining_time_to_full_soc.emplace());
    }
    if (in.RemainingTimeToBulkSoC_isUsed) {
        convert(in.RemainingTimeToBulkSoC, out.remaining_time_to_bulk_soc.emplace());
    }
    convert(in.EVTargetVoltage, out.ev_target_voltage);
}

template <> void convert(const struct iso2_CurrentDemandResType& in, CurrentDemandResponse& out) {
    cb_convert_enum(in.ResponseCode, out.response_code);
    convert(in.DC_EVSEStatus, out.dc_evse_status);
    convert(in.EVSEPresentVoltage, out.evse_present_voltage);
    convert(in.EVSEPresentCurrent, out.evse_present_current);
    out.evse_current_limit_achieved = in.EVSECurrentLimitAchieved;
    out.evse_voltage_limit_achieved = in.EVSEVoltageLimitAchieved;
    out.evse_power_limit_achieved = in.EVSEPowerLimitAchieved;
    if (in.EVSEMaximumVoltageLimit_isUsed) {
        convert(in.EVSEMaximumVoltageLimit, out.evse_maximum_voltage_limit.emplace());
    }
    if (in.EVSEMaximumCurrentLimit_isUsed) {
        convert(in.EVSEMaximumCurrentLimit, out.evse_maximum_current_limit.emplace());
    }
    if (in.EVSEMaximumPowerLimit_isUsed) {
        convert(in.EVSEMaximumPowerLimit, out.evse_maximum_power_limit.emplace());
    }
    out.evse_id = CB2CPP_STRING(in.EVSEID);
    out.sa_schedule_tuple_id = in.SAScheduleTupleID;
    if (in.MeterInfo_isUsed) {
        convert(in.MeterInfo, out.meter_info.emplace());
    }
    if (in.ReceiptRequired_isUsed) {
        out.receipt_required = static_cast<bool>(in.ReceiptRequired);
    }
}

template <> void convert(const CurrentDemandRequest& in, struct iso2_CurrentDemandReqType& out) {
    init_iso2_CurrentDemandReqType(&out);
    convert(in.dc_ev_status, out.DC_EVStatus);
    convert(in.ev_target_current, out.EVTargetCurrent);
    if (in.ev_maximum_voltage_limit) {
        convert(in.ev_maximum_voltage_limit.value(), out.EVMaximumVoltageLimit);
        CB_SET_USED(out.EVMaximumVoltageLimit);
    }
    if (in.ev_maximum_current_limit) {
        convert(in.ev_maximum_current_limit.value(), out.EVMaximumCurrentLimit);
        CB_SET_USED(out.EVMaximumCurrentLimit);
    }
    if (in.ev_maximum_power_limit) {
        convert(in.ev_maximum_power_limit.value(), out.EVMaximumPowerLimit);
        CB_SET_USED(out.EVMaximumPowerLimit);
    }
    if (in.bulk_charging_complete) {
        out.BulkChargingComplete = in.bulk_charging_complete.value();
        CB_SET_USED(out.BulkChargingComplete);
    }
    out.ChargingComplete = in.charging_complete;
    if (in.remaining_time_to_full_soc) {
        convert(in.remaining_time_to_full_soc.value(), out.RemainingTimeToFullSoC);
        CB_SET_USED(out.RemainingTimeToFullSoC);
    }
    if (in.remaining_time_to_bulk_soc) {
        convert(in.remaining_time_to_bulk_soc.value(), out.RemainingTimeToBulkSoC);
        CB_SET_USED(out.RemainingTimeToBulkSoC);
    }
    convert(in.ev_target_voltage, out.EVTargetVoltage);
}

template <> void convert(const CurrentDemandResponse& in, struct iso2_CurrentDemandResType& out) {
    init_iso2_CurrentDemandResType(&out);
    cb_convert_enum(in.response_code, out.ResponseCode);
    convert(in.dc_evse_status, out.DC_EVSEStatus);
    convert(in.evse_present_voltage, out.EVSEPresentVoltage);
    convert(in.evse_present_current, out.EVSEPresentCurrent);
    out.EVSECurrentLimitAchieved = in.evse_current_limit_achieved;
    out.EVSEVoltageLimitAchieved = in.evse_voltage_limit_achieved;
    out.EVSEPowerLimitAchieved = in.evse_power_limit_achieved;
    if (in.evse_maximum_voltage_limit) {
        convert(in.evse_maximum_voltage_limit.value(), out.EVSEMaximumVoltageLimit);
        CB_SET_USED(out.EVSEMaximumVoltageLimit);
    }
    if (in.evse_maximum_current_limit) {
        convert(in.evse_maximum_current_limit.value(), out.EVSEMaximumCurrentLimit);
        CB_SET_USED(out.EVSEMaximumCurrentLimit);
    }
    if (in.evse_maximum_power_limit) {
        convert(in.evse_maximum_power_limit.value(), out.EVSEMaximumPowerLimit);
        CB_SET_USED(out.EVSEMaximumPowerLimit);
    }
    CPP2CB_STRING(in.evse_id, out.EVSEID);
    out.SAScheduleTupleID = in.sa_schedule_tuple_id;
    if (in.meter_info) {
        convert(in.meter_info.value(), out.MeterInfo);
        CB_SET_USED(out.MeterInfo);
    }
    if (in.receipt_required) {
        out.ReceiptRequired = in.receipt_required.value();
        CB_SET_USED(out.ReceiptRequired);
    }
}

template <> void insert_type(VariantAccess& va, const struct iso2_CurrentDemandReqType& in) {
    va.insert_type<CurrentDemandRequest>(in);
}

template <> void insert_type(VariantAccess& va, const struct iso2_CurrentDemandResType& in) {
    va.insert_type<CurrentDemandResponse>(in);
}

template <> int serialize_to_exi(const CurrentDemandRequest& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.CurrentDemandReq);
    convert(in, doc.V2G_Message.Body.CurrentDemandReq);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> int serialize_to_exi(const CurrentDemandResponse& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.CurrentDemandRes);
    convert(in, doc.V2G_Message.Body.CurrentDemandRes);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> size_t serialize(const CurrentDemandRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> size_t serialize(const CurrentDemandResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_2
