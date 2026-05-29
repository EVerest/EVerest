// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/message/d2/session_setup.hpp>

#include <iso15118/message/d2/type.hpp>

#include <cbv2g/iso_2/iso2_msgDefDecoder.h>
#include <cbv2g/iso_2/iso2_msgDefEncoder.h>

#include <iso15118/detail/cb_exi.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::msg {
namespace d2 {

template <> void convert(const struct iso2_SessionSetupReqType& in, SessionSetupRequest& out) {
    std::copy(in.EVCCID.bytes, in.EVCCID.bytes + in.EVCCID.bytesLen, out.evcc_id.begin());
}

template <> void convert(const SessionSetupResponse& in, struct iso2_SessionSetupResType& out) {
    init_iso2_SessionSetupResType(&out);

    cb_convert_enum(in.response_code, out.ResponseCode);
    CPP2CB_STRING(in.evse_id, out.EVSEID);
    CPP2CB_ASSIGN_IF_USED(in.timestamp, out.EVSETimeStamp);
}

template <> size_t serialize(const SessionSetupResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace d2
template <> int serialize_to_exi(const d2::SessionSetupResponse& in, exi_bitstream_t& out) {

    iso2_exiDocument doc{};
    init_iso2_exiDocument(&doc);

    init_iso2_BodyType(&doc.V2G_Message.Body);

    convert(in.header, doc.V2G_Message.Header);

    CB_SET_USED(doc.V2G_Message.Body.SessionSetupRes);
    convert(in, doc.V2G_Message.Body.SessionSetupRes);

    return encode_iso2_exiDocument(&out, &doc);
}

} // namespace iso15118::msg
