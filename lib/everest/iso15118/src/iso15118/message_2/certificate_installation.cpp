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

// Encode direction of the chain, needed for the responses the SECC builds itself (an out-of-sequence
// CertificateInstallationReq); a successful installation is relayed from the backend as raw EXI and
// never passes through here.
template <> void convert(const CertificateChain& in, struct iso2_CertificateChainType& out) {
    init_iso2_CertificateChainType(&out);
    if (in.id.has_value()) {
        CPP2CB_STRING(in.id.value(), out.Id);
        CB_SET_USED(out.Id);
    }
    CPP2CB_BYTES(in.certificate, out.Certificate);
    if (not in.sub_certificates.empty()) {
        auto& subs = out.SubCertificates.Certificate;
        CPP2CB_ARRAY_SIZE_CHECK(in.sub_certificates.size(), subs.array);
        uint16_t index = 0;
        for (const auto& sub : in.sub_certificates) {
            CPP2CB_BYTES(sub, subs.array[index]);
            index++;
        }
        subs.arrayLen = static_cast<uint16_t>(in.sub_certificates.size());
        CB_SET_USED(out.SubCertificates);
    }
}

template <> void convert(const CertificateInstallationResponse& in, struct iso2_CertificateInstallationResType& out) {
    init_iso2_CertificateInstallationResType(&out);
    cb_convert_enum(in.response_code, out.ResponseCode);
    convert(in.sa_provisioning_chain, out.SAProvisioningCertificateChain);
    convert(in.contract_chain, out.ContractSignatureCertChain);
    CPP2CB_BYTES(in.encrypted_private_key, out.ContractSignatureEncryptedPrivateKey.CONTENT);
    CPP2CB_BYTES(in.dh_public_key, out.DHpublickey.CONTENT);
    CPP2CB_STRING(in.emaid, out.eMAID.CONTENT);
    // The Id attributes are mandatory (the signature references them) but not modelled in the C++
    // struct: a relayed response brings its own, a self-built one only needs them to be present.
    CPP2CB_STRING(std::string("contractSignatureEncryptedPrivateKey"), out.ContractSignatureEncryptedPrivateKey.Id);
    CPP2CB_STRING(std::string("dhPublicKey"), out.DHpublickey.Id);
    CPP2CB_STRING(std::string("eMAID"), out.eMAID.Id);
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

template <> int serialize_to_exi(const CertificateInstallationResponse& in, exi_bitstream_t& out) {
    iso2_exiDocument doc{};
    convert(in.header, doc.V2G_Message.Header);
    CB_SET_USED(doc.V2G_Message.Body.CertificateInstallationRes);
    convert(in, doc.V2G_Message.Body.CertificateInstallationRes);
    return encode_iso2_exiDocument(&out, &doc);
}

template <> size_t serialize(const CertificateInstallationResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_2
