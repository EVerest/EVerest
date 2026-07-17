// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_2/metering_receipt.hpp>

#include <algorithm>

#include <iso15118/detail/message_2/variant_access.hpp>

#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>
#include <cbv2g/iso_2/iso2_msgDefEncoder.h>

namespace iso15118::message_2 {

template <> void convert(const struct iso2_MeteringReceiptReqType& in, MeteringReceiptRequest& out) {
    // Decode the signed body SessionID (clamped + zero-padded into the fixed 8-byte model) so the SECC
    // can verify it matches the assigned session id.
    out.session_id.fill(0);
    const auto len = std::min<size_t>(in.SessionID.bytesLen, out.session_id.size());
    std::copy(in.SessionID.bytes, in.SessionID.bytes + len, out.session_id.begin());

    convert(in.MeterInfo, out.meter_info);
    if (in.SAScheduleTupleID_isUsed) {
        out.sa_schedule_tuple_id = in.SAScheduleTupleID;
    } else {
        out.sa_schedule_tuple_id = std::nullopt;
    }
}

template <> void convert(const MeteringReceiptRequest& in, struct iso2_MeteringReceiptReqType& out) {
    init_iso2_MeteringReceiptReqType(&out);
    // The MeteringReceiptReq is signed under Plug-and-Charge; its Id attribute is referenced by the
    // xmldsig SignedInfo, so it is always set (default "id1", assigned by the EV state).
    // Body-level SessionID (fixed 8-byte model).
    std::copy(in.session_id.begin(), in.session_id.end(), out.SessionID.bytes);
    out.SessionID.bytesLen = datatypes::SESSION_ID_LENGTH;
    convert(in.meter_info, out.MeterInfo);
    if (in.sa_schedule_tuple_id.has_value()) {
        out.SAScheduleTupleID = in.sa_schedule_tuple_id.value();
        CB_SET_USED(out.SAScheduleTupleID);
    }
}

template <> void convert(const MeteringReceiptResponse& in, struct iso2_MeteringReceiptResType& out) {
    init_iso2_MeteringReceiptResType(&out);
    cb_convert_enum(in.response_code, out.ResponseCode);
    if (in.dc_evse_status.has_value()) {
        convert(in.dc_evse_status.value(), out.DC_EVSEStatus);
        CB_SET_USED(out.DC_EVSEStatus);
    } else if (in.ac_evse_status.has_value()) {
        convert(in.ac_evse_status.value(), out.AC_EVSEStatus);
        CB_SET_USED(out.AC_EVSEStatus);
    }
}

template <> void insert_type(VariantAccess& va, const struct iso2_MeteringReceiptReqType& in) {
    va.insert_type<MeteringReceiptRequest>(in);
}

template <> int serialize_to_exi(const MeteringReceiptResponse& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.MeteringReceiptRes);
    convert(in, doc.V2G_Message.Body.MeteringReceiptRes);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> size_t serialize(const MeteringReceiptResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_2
