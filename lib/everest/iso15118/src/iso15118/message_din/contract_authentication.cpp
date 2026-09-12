// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_din/contract_authentication.hpp>

#include <iso15118/detail/message_din/variant_access.hpp>

#include <cbv2g/din/din_msgDefDatatypes.h>
#include <cbv2g/din/din_msgDefEncoder.h>

namespace iso15118::message_din {

template <> void convert(const struct din_ContractAuthenticationReqType& in, ContractAuthenticationRequest& out) {
    CB2CPP_STRING_IF_USED(in.Id, out.id);
    CB2CPP_STRING_IF_USED(in.GenChallenge, out.gen_challenge);
}

template <> void convert(const ContractAuthenticationRequest& in, struct din_ContractAuthenticationReqType& out) {
    init_din_ContractAuthenticationReqType(&out);
    CPP2CB_STRING_IF_USED(in.id, out.Id);
    CPP2CB_STRING_IF_USED(in.gen_challenge, out.GenChallenge);
}

template <> void convert(const struct din_ContractAuthenticationResType& in, ContractAuthenticationResponse& out) {
    cb_convert_enum(in.ResponseCode, out.response_code);
    cb_convert_enum(in.EVSEProcessing, out.evse_processing);
}

template <> void convert(const ContractAuthenticationResponse& in, struct din_ContractAuthenticationResType& out) {
    init_din_ContractAuthenticationResType(&out);
    cb_convert_enum(in.response_code, out.ResponseCode);
    cb_convert_enum(in.evse_processing, out.EVSEProcessing);
}

template <> void insert_type(VariantAccess& va, const struct din_ContractAuthenticationReqType& in) {
    va.insert_type<ContractAuthenticationRequest>(in);
}

template <> void insert_type(VariantAccess& va, const struct din_ContractAuthenticationResType& in) {
    va.insert_type<ContractAuthenticationResponse>(in);
}

template <> int serialize_to_exi(const ContractAuthenticationRequest& in, exi_bitstream_t& out) {
    din_exiDocument doc;
    init_din_exiDocument(&doc);
    init_din_BodyType(&doc.V2G_Message.Body);
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.ContractAuthenticationReq);
    convert(in, doc.V2G_Message.Body.ContractAuthenticationReq);
    return encode_din_exiDocument(&out, &doc);
}

template <> int serialize_to_exi(const ContractAuthenticationResponse& in, exi_bitstream_t& out) {
    din_exiDocument doc;
    init_din_exiDocument(&doc);
    init_din_BodyType(&doc.V2G_Message.Body);
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.ContractAuthenticationRes);
    convert(in, doc.V2G_Message.Body.ContractAuthenticationRes);
    return encode_din_exiDocument(&out, &doc);
}

template <> size_t serialize(const ContractAuthenticationRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> size_t serialize(const ContractAuthenticationResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_din
