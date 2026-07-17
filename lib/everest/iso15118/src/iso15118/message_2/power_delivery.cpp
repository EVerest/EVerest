// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_2/power_delivery.hpp>

#include <iso15118/detail/message_2/variant_access.hpp>

#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>
#include <cbv2g/iso_2/iso2_msgDefEncoder.h>

namespace iso15118::message_2 {

static void convert(const struct iso2_ChargingProfileType& in, datatypes::ChargingProfile& out) {
    out.profile_entry.clear();
    for (uint16_t i = 0; i < in.ProfileEntry.arrayLen; i++) {
        const auto& entry = in.ProfileEntry.array[i];
        auto& out_entry = out.profile_entry.emplace_back();
        out_entry.start = entry.ChargingProfileEntryStart;
        convert(entry.ChargingProfileEntryMaxPower, out_entry.max_power);
        CB2CPP_ASSIGN_IF_USED(entry.ChargingProfileEntryMaxNumberOfPhasesInUse, out_entry.max_number_of_phases_in_use);
    }
}

static void convert(const datatypes::ChargingProfile& in, struct iso2_ChargingProfileType& out) {
    init_iso2_ChargingProfileType(&out);
    uint16_t index = 0;
    for (const auto& entry : in.profile_entry) {
        auto& out_entry = out.ProfileEntry.array[index++];
        init_iso2_ProfileEntryType(&out_entry);
        out_entry.ChargingProfileEntryStart = entry.start;
        convert(entry.max_power, out_entry.ChargingProfileEntryMaxPower);
        CPP2CB_ASSIGN_IF_USED(entry.max_number_of_phases_in_use, out_entry.ChargingProfileEntryMaxNumberOfPhasesInUse);
    }
    out.ProfileEntry.arrayLen = in.profile_entry.size();
}

static void convert(const struct iso2_DC_EVPowerDeliveryParameterType& in, datatypes::DC_EVPowerDeliveryParameter& out) {
    convert(in.DC_EVStatus, out.dc_ev_status);
    if (in.BulkChargingComplete_isUsed) {
        out.bulk_charging_complete = static_cast<bool>(in.BulkChargingComplete);
    }
    out.charging_complete = in.ChargingComplete;
}

static void convert(const datatypes::DC_EVPowerDeliveryParameter& in, struct iso2_DC_EVPowerDeliveryParameterType& out) {
    init_iso2_DC_EVPowerDeliveryParameterType(&out);
    convert(in.dc_ev_status, out.DC_EVStatus);
    if (in.bulk_charging_complete) {
        out.BulkChargingComplete = in.bulk_charging_complete.value();
        CB_SET_USED(out.BulkChargingComplete);
    }
    out.ChargingComplete = in.charging_complete;
}

template <> void convert(const struct iso2_PowerDeliveryReqType& in, PowerDeliveryRequest& out) {
    cb_convert_enum(in.ChargeProgress, out.charge_progress);
    out.sa_schedule_tuple_id = in.SAScheduleTupleID;
    if (in.ChargingProfile_isUsed) {
        convert(in.ChargingProfile, out.charging_profile.emplace());
    }
    if (in.DC_EVPowerDeliveryParameter_isUsed) {
        convert(in.DC_EVPowerDeliveryParameter, out.dc_ev_power_delivery_parameter.emplace());
    }
}

template <> void convert(const struct iso2_PowerDeliveryResType& in, PowerDeliveryResponse& out) {
    cb_convert_enum(in.ResponseCode, out.response_code);
    if (in.AC_EVSEStatus_isUsed) {
        convert(in.AC_EVSEStatus, out.ac_evse_status.emplace());
    }
    if (in.DC_EVSEStatus_isUsed) {
        convert(in.DC_EVSEStatus, out.dc_evse_status.emplace());
    }
}

template <> void convert(const PowerDeliveryRequest& in, struct iso2_PowerDeliveryReqType& out) {
    init_iso2_PowerDeliveryReqType(&out);
    cb_convert_enum(in.charge_progress, out.ChargeProgress);
    out.SAScheduleTupleID = in.sa_schedule_tuple_id;
    if (in.charging_profile) {
        convert(in.charging_profile.value(), out.ChargingProfile);
        CB_SET_USED(out.ChargingProfile);
    }
    if (in.dc_ev_power_delivery_parameter) {
        convert(in.dc_ev_power_delivery_parameter.value(), out.DC_EVPowerDeliveryParameter);
        CB_SET_USED(out.DC_EVPowerDeliveryParameter);
    }
}

template <> void convert(const PowerDeliveryResponse& in, struct iso2_PowerDeliveryResType& out) {
    init_iso2_PowerDeliveryResType(&out);
    cb_convert_enum(in.response_code, out.ResponseCode);
    if (in.ac_evse_status) {
        convert(in.ac_evse_status.value(), out.AC_EVSEStatus);
        CB_SET_USED(out.AC_EVSEStatus);
    }
    if (in.dc_evse_status) {
        convert(in.dc_evse_status.value(), out.DC_EVSEStatus);
        CB_SET_USED(out.DC_EVSEStatus);
    }
}

template <> void insert_type(VariantAccess& va, const struct iso2_PowerDeliveryReqType& in) {
    va.insert_type<PowerDeliveryRequest>(in);
}

template <> void insert_type(VariantAccess& va, const struct iso2_PowerDeliveryResType& in) {
    va.insert_type<PowerDeliveryResponse>(in);
}

template <> int serialize_to_exi(const PowerDeliveryRequest& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.PowerDeliveryReq);
    convert(in, doc.V2G_Message.Body.PowerDeliveryReq);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> int serialize_to_exi(const PowerDeliveryResponse& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.PowerDeliveryRes);
    convert(in, doc.V2G_Message.Body.PowerDeliveryRes);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> size_t serialize(const PowerDeliveryRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> size_t serialize(const PowerDeliveryResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_2
