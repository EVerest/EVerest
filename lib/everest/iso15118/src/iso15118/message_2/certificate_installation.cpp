// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_2/certificate_installation.hpp>

#include <iso15118/detail/message_2/variant_access.hpp>

#include <cbv2g/common/exi_basetypes.h>
#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>
#include <cbv2g/iso_2/iso2_msgDefEncoder.h>

namespace iso15118::message_2 {

template <> void convert(const struct iso2_CertificateChainType& in, CertificateChain& out) {
    if (in.Id_isUsed) {
        out.id = CB2CPP_STRING(in.Id);
    }
    out.certificate.assign(in.Certificate.bytes, in.Certificate.bytes + in.Certificate.bytesLen);
    out.sub_certificates.clear();
    if (in.SubCertificates_isUsed) {
        const auto& subs = in.SubCertificates.Certificate;
        for (uint16_t i = 0; i < subs.arrayLen; i++) {
            out.sub_certificates.emplace_back(subs.array[i].bytes, subs.array[i].bytes + subs.array[i].bytesLen);
        }
    }
}

template <> void convert(const struct iso2_CertificateInstallationResType& in, CertificateInstallationResponse& out) {
    cb_convert_enum(in.ResponseCode, out.response_code);
    convert(in.SAProvisioningCertificateChain, out.sa_provisioning_chain);
    convert(in.ContractSignatureCertChain, out.contract_chain);

    const auto& enc = in.ContractSignatureEncryptedPrivateKey.CONTENT;
    out.encrypted_private_key.assign(enc.bytes, enc.bytes + enc.bytesLen);

    const auto& dh = in.DHpublickey.CONTENT;
    out.dh_public_key.assign(dh.bytes, dh.bytes + dh.bytesLen);

    out.emaid = std::string(in.eMAID.CONTENT.characters, in.eMAID.CONTENT.charactersLen);
}

template <> void convert(const CertificateInstallationRequest& in, struct iso2_CertificateInstallationReqType& out) {
    init_iso2_CertificateInstallationReqType(&out);

    // Id is a mandatory attribute of CertificateInstallationReq (no _isUsed flag).
    CPP2CB_STRING(in.id, out.Id);

    CPP2CB_BYTES(in.oem_provisioning_cert, out.OEMProvisioningCert);

    auto& list = out.ListOfRootCertificateIDs.RootCertificateID;
    CPP2CB_ARRAY_SIZE_CHECK(in.root_certificate_ids.size(), list.array);
    uint16_t index = 0;
    for (const auto& rid : in.root_certificate_ids) {
        auto& entry = list.array[index++];
        CPP2CB_STRING(rid.issuer_name, entry.X509IssuerName);
        exi_basetypes_convert_64_to_signed(&entry.X509SerialNumber, rid.serial_number);
    }
    list.arrayLen = in.root_certificate_ids.size();
}

template <> void insert_type(VariantAccess& va, const struct iso2_CertificateInstallationResType& in) {
    va.insert_type<CertificateInstallationResponse>(in);
}

template <> int serialize_to_exi(const CertificateInstallationRequest& in, exi_bitstream_t& out) {
    // Unsigned serialization (used by tests). The production EV path signs the element with the OEM
    // provisioning key via d2::crypto::serialize_signed, which attaches Header.Signature.
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.CertificateInstallationReq);
    convert(in, doc.V2G_Message.Body.CertificateInstallationReq);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> size_t serialize(const CertificateInstallationRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_2
