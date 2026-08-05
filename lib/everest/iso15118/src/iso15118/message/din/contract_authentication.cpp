// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/message/din/contract_authentication.hpp>

#include <iso15118/detail/variant_access.hpp>

#include <cbv2g/din/din_msgDefDecoder.h>
#include <cbv2g/din/din_msgDefEncoder.h>

#include <iso15118/detail/helper.hpp>

namespace iso15118::din::msg {

template <> void convert(const struct din_ContractAuthenticationReqType& in, ContractAuthenticationRequest& out) {
    CB2CPP_STRING_IF_USED(in.Id, out.id);
    CB2CPP_STRING_IF_USED(in.GenChallenge, out.gen_challenge);
}

template <>
void insert_type(VariantAccess& va, const struct din_ContractAuthenticationReqType& in,
                 const struct din_MessageHeaderType& header) {
    va.insert_type<ContractAuthenticationRequest>(in, header);
}

template <> void convert(const ContractAuthenticationResponse& in, struct din_ContractAuthenticationResType& out) {
    init_din_ContractAuthenticationResType(&out);

    cb_convert_enum(in.response_code, out.ResponseCode);
    cb_convert_enum(in.evse_processing, out.EVSEProcessing);
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

template <> size_t serialize(const ContractAuthenticationResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::din::msg
