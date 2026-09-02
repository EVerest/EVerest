// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_2/session_stop.hpp>

#include <iso15118/detail/message_2/variant_access.hpp>

#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>
#include <cbv2g/iso_2/iso2_msgDefEncoder.h>

namespace iso15118::message_2 {

template <> void convert(const struct iso2_SessionStopReqType& in, SessionStopRequest& out) {
    cb_convert_enum(in.ChargingSession, out.charging_session);
}

template <> void convert(const struct iso2_SessionStopResType& in, SessionStopResponse& out) {
    cb_convert_enum(in.ResponseCode, out.response_code);
}

template <> void convert(const SessionStopRequest& in, struct iso2_SessionStopReqType& out) {
    init_iso2_SessionStopReqType(&out);
    cb_convert_enum(in.charging_session, out.ChargingSession);
}

template <> void convert(const SessionStopResponse& in, struct iso2_SessionStopResType& out) {
    init_iso2_SessionStopResType(&out);
    cb_convert_enum(in.response_code, out.ResponseCode);
}

template <> void insert_type(VariantAccess& va, const struct iso2_SessionStopReqType& in) {
    va.insert_type<SessionStopRequest>(in);
}

template <> void insert_type(VariantAccess& va, const struct iso2_SessionStopResType& in) {
    va.insert_type<SessionStopResponse>(in);
}

template <> int serialize_to_exi(const SessionStopRequest& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.SessionStopReq);
    convert(in, doc.V2G_Message.Body.SessionStopReq);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> int serialize_to_exi(const SessionStopResponse& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.SessionStopRes);
    convert(in, doc.V2G_Message.Body.SessionStopRes);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> size_t serialize(const SessionStopRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> size_t serialize(const SessionStopResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_2
