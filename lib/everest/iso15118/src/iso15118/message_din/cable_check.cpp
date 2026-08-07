// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_din/cable_check.hpp>

#include <iso15118/detail/message_din/variant_access.hpp>

#include <cbv2g/din/din_msgDefDatatypes.h>
#include <cbv2g/din/din_msgDefEncoder.h>

namespace iso15118::message_din {

template <> void convert(const struct din_CableCheckReqType& in, CableCheckRequest& out) {
    convert(in.DC_EVStatus, out.dc_ev_status);
}

template <> void convert(const CableCheckRequest& in, struct din_CableCheckReqType& out) {
    init_din_CableCheckReqType(&out);
    convert(in.dc_ev_status, out.DC_EVStatus);
}

template <> void convert(const struct din_CableCheckResType& in, CableCheckResponse& out) {
    cb_convert_enum(in.ResponseCode, out.response_code);
    convert(in.DC_EVSEStatus, out.dc_evse_status);
    cb_convert_enum(in.EVSEProcessing, out.evse_processing);
}

template <> void convert(const CableCheckResponse& in, struct din_CableCheckResType& out) {
    init_din_CableCheckResType(&out);
    cb_convert_enum(in.response_code, out.ResponseCode);
    convert(in.dc_evse_status, out.DC_EVSEStatus);
    cb_convert_enum(in.evse_processing, out.EVSEProcessing);
}

template <> void insert_type(VariantAccess& va, const struct din_CableCheckReqType& in) {
    va.insert_type<CableCheckRequest>(in);
}

template <> void insert_type(VariantAccess& va, const struct din_CableCheckResType& in) {
    va.insert_type<CableCheckResponse>(in);
}

template <> int serialize_to_exi(const CableCheckRequest& in, exi_bitstream_t& out) {
    din_exiDocument doc;
    init_din_exiDocument(&doc);
    init_din_BodyType(&doc.V2G_Message.Body);
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.CableCheckReq);
    convert(in, doc.V2G_Message.Body.CableCheckReq);
    return encode_din_exiDocument(&out, &doc);
}

template <> int serialize_to_exi(const CableCheckResponse& in, exi_bitstream_t& out) {
    din_exiDocument doc;
    init_din_exiDocument(&doc);
    init_din_BodyType(&doc.V2G_Message.Body);
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.CableCheckRes);
    convert(in, doc.V2G_Message.Body.CableCheckRes);
    return encode_din_exiDocument(&out, &doc);
}

template <> size_t serialize(const CableCheckRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> size_t serialize(const CableCheckResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_din
