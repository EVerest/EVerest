// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_2/authorization.hpp>

#include <iso15118/detail/message_2/variant_access.hpp>

#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>
#include <cbv2g/iso_2/iso2_msgDefEncoder.h>

namespace iso15118::message_2 {

template <> void convert(const struct iso2_AuthorizationReqType& in, AuthorizationRequest& out) {
    CB2CPP_STRING_IF_USED(in.Id, out.id);
    if (in.GenChallenge_isUsed) {
        CB2CPP_BYTES(in.GenChallenge, out.gen_challenge.emplace());
    }
}

template <> void convert(const struct iso2_AuthorizationResType& in, AuthorizationResponse& out) {
    cb_convert_enum(in.ResponseCode, out.response_code);
    cb_convert_enum(in.EVSEProcessing, out.evse_processing);
}

template <> void convert(const AuthorizationRequest& in, struct iso2_AuthorizationReqType& out) {
    init_iso2_AuthorizationReqType(&out);
    CPP2CB_STRING_IF_USED(in.id, out.Id);
    if (in.gen_challenge) {
        CPP2CB_BYTES(in.gen_challenge.value(), out.GenChallenge);
        CB_SET_USED(out.GenChallenge);
    }
}

template <> void convert(const AuthorizationResponse& in, struct iso2_AuthorizationResType& out) {
    init_iso2_AuthorizationResType(&out);
    cb_convert_enum(in.response_code, out.ResponseCode);
    cb_convert_enum(in.evse_processing, out.EVSEProcessing);
}

template <> void insert_type(VariantAccess& va, const struct iso2_AuthorizationReqType& in) {
    va.insert_type<AuthorizationRequest>(in);
}

template <> void insert_type(VariantAccess& va, const struct iso2_AuthorizationResType& in) {
    va.insert_type<AuthorizationResponse>(in);
}

template <> int serialize_to_exi(const AuthorizationRequest& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.AuthorizationReq);
    convert(in, doc.V2G_Message.Body.AuthorizationReq);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> int serialize_to_exi(const AuthorizationResponse& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.AuthorizationRes);
    convert(in, doc.V2G_Message.Body.AuthorizationRes);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> size_t serialize(const AuthorizationRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> size_t serialize(const AuthorizationResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_2
