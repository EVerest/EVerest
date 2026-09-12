// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_2/charging_status.hpp>

#include <iso15118/detail/message_2/variant_access.hpp>

#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>
#include <cbv2g/iso_2/iso2_msgDefEncoder.h>

namespace iso15118::message_2 {

template <> void convert(const struct iso2_ChargingStatusReqType&, ChargingStatusRequest&) {
    // empty request
}

template <> void convert(const struct iso2_ChargingStatusResType& in, ChargingStatusResponse& out) {
    cb_convert_enum(in.ResponseCode, out.response_code);
    out.evse_id = CB2CPP_STRING(in.EVSEID);
    out.sa_schedule_tuple_id = in.SAScheduleTupleID;
    if (in.EVSEMaxCurrent_isUsed) {
        convert(in.EVSEMaxCurrent, out.evse_max_current.emplace());
    }
    if (in.MeterInfo_isUsed) {
        convert(in.MeterInfo, out.meter_info.emplace());
    }
    if (in.ReceiptRequired_isUsed) {
        out.receipt_required = static_cast<bool>(in.ReceiptRequired);
    }
    convert(in.AC_EVSEStatus, out.ac_evse_status);
}

template <> void convert(const ChargingStatusRequest&, struct iso2_ChargingStatusReqType& out) {
    init_iso2_ChargingStatusReqType(&out);
}

template <> void convert(const ChargingStatusResponse& in, struct iso2_ChargingStatusResType& out) {
    init_iso2_ChargingStatusResType(&out);
    cb_convert_enum(in.response_code, out.ResponseCode);
    CPP2CB_STRING(in.evse_id, out.EVSEID);
    out.SAScheduleTupleID = in.sa_schedule_tuple_id;
    if (in.evse_max_current) {
        convert(in.evse_max_current.value(), out.EVSEMaxCurrent);
        CB_SET_USED(out.EVSEMaxCurrent);
    }
    if (in.meter_info) {
        convert(in.meter_info.value(), out.MeterInfo);
        CB_SET_USED(out.MeterInfo);
    }
    if (in.receipt_required) {
        out.ReceiptRequired = in.receipt_required.value();
        CB_SET_USED(out.ReceiptRequired);
    }
    convert(in.ac_evse_status, out.AC_EVSEStatus);
}

template <> void insert_type(VariantAccess& va, const struct iso2_ChargingStatusReqType& in) {
    va.insert_type<ChargingStatusRequest>(in);
}

template <> void insert_type(VariantAccess& va, const struct iso2_ChargingStatusResType& in) {
    va.insert_type<ChargingStatusResponse>(in);
}

template <> int serialize_to_exi(const ChargingStatusRequest& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.ChargingStatusReq);
    convert(in, doc.V2G_Message.Body.ChargingStatusReq);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> int serialize_to_exi(const ChargingStatusResponse& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.ChargingStatusRes);
    convert(in, doc.V2G_Message.Body.ChargingStatusRes);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> size_t serialize(const ChargingStatusRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> size_t serialize(const ChargingStatusResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_2
