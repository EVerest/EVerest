// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_2/payment_details.hpp>

#include <iso15118/detail/message_2/variant_access.hpp>

#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>
#include <cbv2g/iso_2/iso2_msgDefEncoder.h>

namespace iso15118::message_2 {

template <> void convert(const struct iso2_PaymentDetailsReqType& in, PaymentDetailsRequest& out) {
    out.emaid = std::string(in.eMAID.characters, in.eMAID.charactersLen);

    const auto& leaf = in.ContractSignatureCertChain.Certificate;
    out.contract_certificate.assign(leaf.bytes, leaf.bytes + leaf.bytesLen);

    out.sub_certificates.clear();
    if (in.ContractSignatureCertChain.SubCertificates_isUsed) {
        const auto& subs = in.ContractSignatureCertChain.SubCertificates.Certificate;
        for (uint16_t i = 0; i < subs.arrayLen; i++) {
            out.sub_certificates.emplace_back(subs.array[i].bytes, subs.array[i].bytes + subs.array[i].bytesLen);
        }
    }
}

template <> void convert(const struct iso2_PaymentDetailsResType& in, PaymentDetailsResponse& out) {
    cb_convert_enum(in.ResponseCode, out.response_code);
    CB2CPP_BYTES(in.GenChallenge, out.gen_challenge);
    out.evse_timestamp = in.EVSETimeStamp;
}

template <> void convert(const PaymentDetailsRequest& in, struct iso2_PaymentDetailsReqType& out) {
    init_iso2_PaymentDetailsReqType(&out);
    CPP2CB_STRING(in.emaid, out.eMAID);

    auto& chain = out.ContractSignatureCertChain;
    CPP2CB_BYTES(in.contract_certificate, chain.Certificate);

    if (not in.sub_certificates.empty()) {
        uint16_t index = 0;
        for (const auto& sub : in.sub_certificates) {
            auto& out_sub = chain.SubCertificates.Certificate.array[index++];
            CPP2CB_BYTES(sub, out_sub);
        }
        chain.SubCertificates.Certificate.arrayLen = in.sub_certificates.size();
        CB_SET_USED(chain.SubCertificates);
    }
}

template <> void convert(const PaymentDetailsResponse& in, struct iso2_PaymentDetailsResType& out) {
    init_iso2_PaymentDetailsResType(&out);
    cb_convert_enum(in.response_code, out.ResponseCode);
    CPP2CB_BYTES(in.gen_challenge, out.GenChallenge);
    out.EVSETimeStamp = in.evse_timestamp;
}

template <> void insert_type(VariantAccess& va, const struct iso2_PaymentDetailsReqType& in) {
    va.insert_type<PaymentDetailsRequest>(in);
}

template <> void insert_type(VariantAccess& va, const struct iso2_PaymentDetailsResType& in) {
    va.insert_type<PaymentDetailsResponse>(in);
}

template <> int serialize_to_exi(const PaymentDetailsRequest& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.PaymentDetailsReq);
    convert(in, doc.V2G_Message.Body.PaymentDetailsReq);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> int serialize_to_exi(const PaymentDetailsResponse& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.PaymentDetailsRes);
    convert(in, doc.V2G_Message.Body.PaymentDetailsRes);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> size_t serialize(const PaymentDetailsRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> size_t serialize(const PaymentDetailsResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_2
