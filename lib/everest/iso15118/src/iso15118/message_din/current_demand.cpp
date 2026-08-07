// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_din/current_demand.hpp>

#include <iso15118/detail/message_din/variant_access.hpp>

#include <cbv2g/din/din_msgDefDatatypes.h>
#include <cbv2g/din/din_msgDefEncoder.h>

namespace iso15118::message_din {

using datatypes::Unit;

template <> void convert(const struct din_CurrentDemandReqType& in, CurrentDemandRequest& out) {
    convert(in.DC_EVStatus, out.dc_ev_status);
    out.ev_target_current = from_physical_value(in.EVTargetCurrent);
    out.ev_target_voltage = from_physical_value(in.EVTargetVoltage);
    if (in.EVMaximumVoltageLimit_isUsed) {
        out.ev_maximum_voltage_limit = from_physical_value(in.EVMaximumVoltageLimit);
    }
    if (in.EVMaximumCurrentLimit_isUsed) {
        out.ev_maximum_current_limit = from_physical_value(in.EVMaximumCurrentLimit);
    }
    if (in.EVMaximumPowerLimit_isUsed) {
        out.ev_maximum_power_limit = from_physical_value(in.EVMaximumPowerLimit);
    }
    CB2CPP_ASSIGN_IF_USED(in.BulkChargingComplete, out.bulk_charging_complete);
    out.charging_complete = in.ChargingComplete;
    if (in.RemainingTimeToFullSoC_isUsed) {
        out.remaining_time_to_full_soc = from_physical_value(in.RemainingTimeToFullSoC);
    }
    if (in.RemainingTimeToBulkSoC_isUsed) {
        out.remaining_time_to_bulk_soc = from_physical_value(in.RemainingTimeToBulkSoC);
    }
}

template <> void convert(const CurrentDemandRequest& in, struct din_CurrentDemandReqType& out) {
    init_din_CurrentDemandReqType(&out);
    convert(in.dc_ev_status, out.DC_EVStatus);
    out.EVTargetCurrent = to_physical_value(in.ev_target_current, Unit::A);
    out.EVTargetVoltage = to_physical_value(in.ev_target_voltage, Unit::V);
    if (in.ev_maximum_voltage_limit) {
        out.EVMaximumVoltageLimit = to_physical_value(in.ev_maximum_voltage_limit.value(), Unit::V);
        CB_SET_USED(out.EVMaximumVoltageLimit);
    }
    if (in.ev_maximum_current_limit) {
        out.EVMaximumCurrentLimit = to_physical_value(in.ev_maximum_current_limit.value(), Unit::A);
        CB_SET_USED(out.EVMaximumCurrentLimit);
    }
    if (in.ev_maximum_power_limit) {
        out.EVMaximumPowerLimit = to_physical_value(in.ev_maximum_power_limit.value(), Unit::W);
        CB_SET_USED(out.EVMaximumPowerLimit);
    }
    CPP2CB_ASSIGN_IF_USED(in.bulk_charging_complete, out.BulkChargingComplete);
    out.ChargingComplete = in.charging_complete;
    if (in.remaining_time_to_full_soc) {
        out.RemainingTimeToFullSoC = to_physical_value(in.remaining_time_to_full_soc.value(), Unit::s);
        CB_SET_USED(out.RemainingTimeToFullSoC);
    }
    if (in.remaining_time_to_bulk_soc) {
        out.RemainingTimeToBulkSoC = to_physical_value(in.remaining_time_to_bulk_soc.value(), Unit::s);
        CB_SET_USED(out.RemainingTimeToBulkSoC);
    }
}

template <> void convert(const struct din_CurrentDemandResType& in, CurrentDemandResponse& out) {
    cb_convert_enum(in.ResponseCode, out.response_code);
    convert(in.DC_EVSEStatus, out.dc_evse_status);
    out.evse_present_voltage = from_physical_value(in.EVSEPresentVoltage);
    out.evse_present_current = from_physical_value(in.EVSEPresentCurrent);
    out.evse_current_limit_achieved = in.EVSECurrentLimitAchieved;
    out.evse_voltage_limit_achieved = in.EVSEVoltageLimitAchieved;
    out.evse_power_limit_achieved = in.EVSEPowerLimitAchieved;
    if (in.EVSEMaximumVoltageLimit_isUsed) {
        out.evse_maximum_voltage_limit = from_physical_value(in.EVSEMaximumVoltageLimit);
    }
    if (in.EVSEMaximumCurrentLimit_isUsed) {
        out.evse_maximum_current_limit = from_physical_value(in.EVSEMaximumCurrentLimit);
    }
    if (in.EVSEMaximumPowerLimit_isUsed) {
        out.evse_maximum_power_limit = from_physical_value(in.EVSEMaximumPowerLimit);
    }
}

template <> void convert(const CurrentDemandResponse& in, struct din_CurrentDemandResType& out) {
    init_din_CurrentDemandResType(&out);
    cb_convert_enum(in.response_code, out.ResponseCode);
    convert(in.dc_evse_status, out.DC_EVSEStatus);
    out.EVSEPresentVoltage = to_physical_value(in.evse_present_voltage, Unit::V);
    out.EVSEPresentCurrent = to_physical_value(in.evse_present_current, Unit::A);
    out.EVSECurrentLimitAchieved = in.evse_current_limit_achieved;
    out.EVSEVoltageLimitAchieved = in.evse_voltage_limit_achieved;
    out.EVSEPowerLimitAchieved = in.evse_power_limit_achieved;
    if (in.evse_maximum_voltage_limit) {
        out.EVSEMaximumVoltageLimit = to_physical_value(in.evse_maximum_voltage_limit.value(), Unit::V);
        CB_SET_USED(out.EVSEMaximumVoltageLimit);
    }
    if (in.evse_maximum_current_limit) {
        out.EVSEMaximumCurrentLimit = to_physical_value(in.evse_maximum_current_limit.value(), Unit::A);
        CB_SET_USED(out.EVSEMaximumCurrentLimit);
    }
    if (in.evse_maximum_power_limit) {
        out.EVSEMaximumPowerLimit = to_physical_value(in.evse_maximum_power_limit.value(), Unit::W);
        CB_SET_USED(out.EVSEMaximumPowerLimit);
    }
}

template <> void insert_type(VariantAccess& va, const struct din_CurrentDemandReqType& in) {
    va.insert_type<CurrentDemandRequest>(in);
}

template <> void insert_type(VariantAccess& va, const struct din_CurrentDemandResType& in) {
    va.insert_type<CurrentDemandResponse>(in);
}

template <> int serialize_to_exi(const CurrentDemandRequest& in, exi_bitstream_t& out) {
    din_exiDocument doc;
    init_din_exiDocument(&doc);
    init_din_BodyType(&doc.V2G_Message.Body);
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.CurrentDemandReq);
    convert(in, doc.V2G_Message.Body.CurrentDemandReq);
    return encode_din_exiDocument(&out, &doc);
}

template <> int serialize_to_exi(const CurrentDemandResponse& in, exi_bitstream_t& out) {
    din_exiDocument doc;
    init_din_exiDocument(&doc);
    init_din_BodyType(&doc.V2G_Message.Body);
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.CurrentDemandRes);
    convert(in, doc.V2G_Message.Body.CurrentDemandRes);
    return encode_din_exiDocument(&out, &doc);
}

template <> size_t serialize(const CurrentDemandRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> size_t serialize(const CurrentDemandResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_din
