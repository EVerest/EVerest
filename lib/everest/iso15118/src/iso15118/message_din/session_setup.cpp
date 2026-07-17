// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_din/session_setup.hpp>

#include <iso15118/detail/message_din/variant_access.hpp>

#include <cbv2g/din/din_msgDefDatatypes.h>
#include <cbv2g/din/din_msgDefEncoder.h>

namespace iso15118::message_din {

template <> void convert(const struct din_SessionSetupReqType& in, SessionSetupRequest& out) {
    out.evcc_id.assign(in.EVCCID.bytes, in.EVCCID.bytes + in.EVCCID.bytesLen);
}

template <> void convert(const SessionSetupRequest& in, struct din_SessionSetupReqType& out) {
    init_din_SessionSetupReqType(&out);
    CPP2CB_BYTES(in.evcc_id, out.EVCCID);
}

template <> void convert(const struct din_SessionSetupResType& in, SessionSetupResponse& out) {
    cb_convert_enum(in.ResponseCode, out.response_code);
    out.evse_id.assign(in.EVSEID.bytes, in.EVSEID.bytes + in.EVSEID.bytesLen);
    CB2CPP_ASSIGN_IF_USED(in.DateTimeNow, out.datetime_now);
}

template <> void convert(const SessionSetupResponse& in, struct din_SessionSetupResType& out) {
    init_din_SessionSetupResType(&out);
    cb_convert_enum(in.response_code, out.ResponseCode);
    CPP2CB_BYTES(in.evse_id, out.EVSEID);
    CPP2CB_ASSIGN_IF_USED(in.datetime_now, out.DateTimeNow);
}

template <> void insert_type(VariantAccess& va, const struct din_SessionSetupReqType& in) {
    va.insert_type<SessionSetupRequest>(in);
}

template <> void insert_type(VariantAccess& va, const struct din_SessionSetupResType& in) {
    va.insert_type<SessionSetupResponse>(in);
}

template <> int serialize_to_exi(const SessionSetupRequest& in, exi_bitstream_t& out) {
    din_exiDocument doc;
    init_din_exiDocument(&doc);
    init_din_BodyType(&doc.V2G_Message.Body);
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.SessionSetupReq);
    convert(in, doc.V2G_Message.Body.SessionSetupReq);
    return encode_din_exiDocument(&out, &doc);
}

template <> int serialize_to_exi(const SessionSetupResponse& in, exi_bitstream_t& out) {
    din_exiDocument doc;
    init_din_exiDocument(&doc);
    init_din_BodyType(&doc.V2G_Message.Body);
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.SessionSetupRes);
    convert(in, doc.V2G_Message.Body.SessionSetupRes);
    return encode_din_exiDocument(&out, &doc);
}

template <> size_t serialize(const SessionSetupRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> size_t serialize(const SessionSetupResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_din
