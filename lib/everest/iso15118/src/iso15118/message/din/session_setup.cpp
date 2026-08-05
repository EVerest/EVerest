// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/message/din/session_setup.hpp>

#include <iso15118/detail/variant_access.hpp>

#include <cbv2g/din/din_msgDefDecoder.h>
#include <cbv2g/din/din_msgDefEncoder.h>

#include <iso15118/detail/helper.hpp>

namespace iso15118::din::msg {

template <> void convert(const struct din_SessionSetupReqType& in, SessionSetupRequest& out) {
    std::copy(in.EVCCID.bytes, in.EVCCID.bytes + in.EVCCID.bytesLen, std::back_inserter(out.evcc_id));
}

template <>
void insert_type(VariantAccess& va, const struct din_SessionSetupReqType& in,
                 const struct din_MessageHeaderType& header) {
    va.insert_type<SessionSetupRequest>(in, header);
}

template <> void convert(const SessionSetupResponse& in, struct din_SessionSetupResType& out) {
    init_din_SessionSetupResType(&out);

    cb_convert_enum(in.response_code, out.ResponseCode);
    CPP2CB_BYTES(in.evse_id, out.EVSEID);
    CPP2CB_ASSIGN_IF_USED(in.date_time_now, out.DateTimeNow);
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

template <> size_t serialize(const SessionSetupResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::din::msg

