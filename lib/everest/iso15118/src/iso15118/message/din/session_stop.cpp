// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/message/din/session_stop.hpp>

#include <iso15118/detail/variant_access.hpp>

#include <cbv2g/din/din_msgDefDecoder.h>
#include <cbv2g/din/din_msgDefEncoder.h>

#include <iso15118/detail/helper.hpp>

namespace iso15118::din::msg {

template <>
void convert([[maybe_unused]] const struct din_SessionStopType& in, [[maybe_unused]] SessionStopRequest& out) {
}

template <>
void insert_type(VariantAccess& va, const struct din_SessionStopType& in, const struct din_MessageHeaderType& header) {
    va.insert_type<SessionStopRequest>(in, header);
}

template <> void convert(const SessionStopResponse& in, struct din_SessionStopResType& out) {
    init_din_SessionStopResType(&out);

    cb_convert_enum(in.response_code, out.ResponseCode);
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

template <> size_t serialize(const SessionStopResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::din::msg
