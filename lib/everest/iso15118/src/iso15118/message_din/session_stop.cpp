// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_din/session_stop.hpp>

#include <iso15118/detail/message_din/variant_access.hpp>

#include <cbv2g/din/din_msgDefDatatypes.h>
#include <cbv2g/din/din_msgDefEncoder.h>

namespace iso15118::message_din {

template <> void convert(const struct din_SessionStopType& in, SessionStopRequest& out) {
    (void)in;
    (void)out;
}

template <> void convert(const SessionStopRequest& in, struct din_SessionStopType& out) {
    (void)in;
    init_din_SessionStopType(&out);
}

template <> void convert(const struct din_SessionStopResType& in, SessionStopResponse& out) {
    cb_convert_enum(in.ResponseCode, out.response_code);
}

template <> void convert(const SessionStopResponse& in, struct din_SessionStopResType& out) {
    init_din_SessionStopResType(&out);
    cb_convert_enum(in.response_code, out.ResponseCode);
}

template <> void insert_type(VariantAccess& va, const struct din_SessionStopType& in) {
    va.insert_type<SessionStopRequest>(in);
}

template <> void insert_type(VariantAccess& va, const struct din_SessionStopResType& in) {
    va.insert_type<SessionStopResponse>(in);
}

template <> int serialize_to_exi(const SessionStopRequest& in, exi_bitstream_t& out) {
    din_exiDocument doc;
    init_din_exiDocument(&doc);
    init_din_BodyType(&doc.V2G_Message.Body);
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.SessionStopReq);
    convert(in, doc.V2G_Message.Body.SessionStopReq);
    return encode_din_exiDocument(&out, &doc);
}

template <> int serialize_to_exi(const SessionStopResponse& in, exi_bitstream_t& out) {
    din_exiDocument doc;
    init_din_exiDocument(&doc);
    init_din_BodyType(&doc.V2G_Message.Body);
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.SessionStopRes);
    convert(in, doc.V2G_Message.Body.SessionStopRes);
    return encode_din_exiDocument(&out, &doc);
}

template <> size_t serialize(const SessionStopRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> size_t serialize(const SessionStopResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_din
