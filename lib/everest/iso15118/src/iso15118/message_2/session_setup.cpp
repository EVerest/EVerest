// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_2/session_setup.hpp>

#include <iso15118/detail/message_2/variant_access.hpp>

#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>
#include <cbv2g/iso_2/iso2_msgDefEncoder.h>

namespace iso15118::message_2 {

template <> void convert(const struct iso2_SessionSetupReqType& in, SessionSetupRequest& out) {
    CB2CPP_BYTES(in.EVCCID, out.evcc_id);
}

template <> void convert(const struct iso2_SessionSetupResType& in, SessionSetupResponse& out) {
    cb_convert_enum(in.ResponseCode, out.response_code);
    out.evse_id = CB2CPP_STRING(in.EVSEID);
    CB2CPP_ASSIGN_IF_USED(in.EVSETimeStamp, out.evse_timestamp);
}

template <> void convert(const SessionSetupRequest& in, struct iso2_SessionSetupReqType& out) {
    init_iso2_SessionSetupReqType(&out);
    CPP2CB_BYTES(in.evcc_id, out.EVCCID);
}

template <> void convert(const SessionSetupResponse& in, struct iso2_SessionSetupResType& out) {
    init_iso2_SessionSetupResType(&out);
    cb_convert_enum(in.response_code, out.ResponseCode);
    CPP2CB_STRING(in.evse_id, out.EVSEID);
    CPP2CB_ASSIGN_IF_USED(in.evse_timestamp, out.EVSETimeStamp);
}

template <> void insert_type(VariantAccess& va, const struct iso2_SessionSetupReqType& in) {
    va.insert_type<SessionSetupRequest>(in);
}

template <> void insert_type(VariantAccess& va, const struct iso2_SessionSetupResType& in) {
    va.insert_type<SessionSetupResponse>(in);
}

template <> int serialize_to_exi(const SessionSetupRequest& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.SessionSetupReq);
    convert(in, doc.V2G_Message.Body.SessionSetupReq);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> int serialize_to_exi(const SessionSetupResponse& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.SessionSetupRes);
    convert(in, doc.V2G_Message.Body.SessionSetupRes);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> size_t serialize(const SessionSetupRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> size_t serialize(const SessionSetupResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_2
