// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/message/certificate_installation.hpp>

#include <iso15118/detail/variant_access.hpp>
#include <stdexcept>

#include <cbv2g/iso_20/iso20_CommonMessages_Encoder.h>

namespace iso15118::message_20 {

namespace {

template <typename cb_SubCertificatesType>
void sub_certificates_from_cb(const cb_SubCertificatesType& in, datatypes::SubCertificate& out) {
    out.clear();
    for (uint16_t i = 0; i < in.Certificate.arrayLen and i < out.capacity(); ++i) {
        const auto& cert = in.Certificate.array[i];
        out.emplace_back(cert.bytes, cert.bytes + cert.bytesLen);
    }
}

template <typename cb_SubCertificatesType>
void sub_certificates_to_cb(const datatypes::SubCertificate& in, cb_SubCertificatesType& out) {
    CPP2CB_ARRAY_SIZE_CHECK(in.size(), out.Certificate.array);
    out.Certificate.arrayLen = static_cast<uint16_t>(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        CPP2CB_BYTES(in[i], out.Certificate.array[i]);
    }
}

template <typename cb_BytesType> std::vector<uint8_t> bytes_from_cb(const cb_BytesType& in) {
    return std::vector<uint8_t>(in.bytes, in.bytes + in.bytesLen);
}

void convert_chain(const iso20_SignedCertificateChainType& in, datatypes::SignedCertificateChain& out) {
    out.id = CB2CPP_STRING(in.Id);
    out.certificate = bytes_from_cb(in.Certificate);
    out.sub_certificates.clear();
    if (in.SubCertificates_isUsed) {
        sub_certificates_from_cb(in.SubCertificates, out.sub_certificates);
    }
}

void convert_chain(const datatypes::SignedCertificateChain& in, iso20_SignedCertificateChainType& out) {
    init_iso20_SignedCertificateChainType(&out);
    CPP2CB_STRING(in.id, out.Id);
    CPP2CB_BYTES(in.certificate, out.Certificate);
    if (not in.sub_certificates.empty()) {
        CB_SET_USED(out.SubCertificates);
        sub_certificates_to_cb(in.sub_certificates, out.SubCertificates);
    }
}

void convert_chain(const iso20_CertificateChainType& in, datatypes::CertificateChain& out) {
    out.certificate = bytes_from_cb(in.Certificate);
    out.sub_certificates.clear();
    if (in.SubCertificates_isUsed) {
        sub_certificates_from_cb(in.SubCertificates, out.sub_certificates);
    }
}

void convert_chain(const datatypes::CertificateChain& in, iso20_CertificateChainType& out) {
    init_iso20_CertificateChainType(&out);
    CPP2CB_BYTES(in.certificate, out.Certificate);
    if (not in.sub_certificates.empty()) {
        CB_SET_USED(out.SubCertificates);
        sub_certificates_to_cb(in.sub_certificates, out.SubCertificates);
    }
}

void convert_root_ids(const iso20_ListOfRootCertificateIDsType& in, datatypes::ListOfRootCertificateIDs& out) {
    out.root_certificate_id.clear();
    for (uint16_t i = 0; i < in.RootCertificateID.arrayLen and i < out.root_certificate_id.capacity(); ++i) {
        const auto& cb_id = in.RootCertificateID.array[i];
        auto& id = out.root_certificate_id.emplace_back();
        id.issuer_name = CB2CPP_STRING(cb_id.X509IssuerName);
        exi_basetypes_convert_64_from_signed(&cb_id.X509SerialNumber, &id.serial_number);
    }
}

void convert_root_ids(const datatypes::ListOfRootCertificateIDs& in, iso20_ListOfRootCertificateIDsType& out) {
    init_iso20_ListOfRootCertificateIDsType(&out);
    CPP2CB_ARRAY_SIZE_CHECK(in.root_certificate_id.size(), out.RootCertificateID.array);
    out.RootCertificateID.arrayLen = static_cast<uint16_t>(in.root_certificate_id.size());
    for (std::size_t i = 0; i < in.root_certificate_id.size(); ++i) {
        auto& cb_id = out.RootCertificateID.array[i];
        init_iso20_X509IssuerSerialType(&cb_id);
        CPP2CB_STRING(in.root_certificate_id[i].issuer_name, cb_id.X509IssuerName);
        exi_basetypes_convert_64_to_signed(&cb_id.X509SerialNumber, in.root_certificate_id[i].serial_number);
    }
}

void convert_installation_data(const iso20_SignedInstallationDataType& in, datatypes::SignedInstallationData& out) {
    out.id = CB2CPP_STRING(in.Id);
    convert(in.ContractCertificateChain, out.contract_certificate_chain);
    cb_convert_enum(in.ECDHCurve, out.ecdh_curve);
    out.dh_public_key = bytes_from_cb(in.DHPublicKey);
    out.secp521_encrypted_private_key.reset();
    out.x448_encrypted_private_key.reset();
    out.tpm_encrypted_private_key.reset();
    if (in.SECP521_EncryptedPrivateKey_isUsed) {
        out.secp521_encrypted_private_key = bytes_from_cb(in.SECP521_EncryptedPrivateKey);
    }
    if (in.X448_EncryptedPrivateKey_isUsed) {
        out.x448_encrypted_private_key = bytes_from_cb(in.X448_EncryptedPrivateKey);
    }
    if (in.TPM_EncryptedPrivateKey_isUsed) {
        out.tpm_encrypted_private_key = bytes_from_cb(in.TPM_EncryptedPrivateKey);
    }
}

void convert_installation_data(const datatypes::SignedInstallationData& in, iso20_SignedInstallationDataType& out) {
    out = {};
    init_iso20_SignedInstallationDataType(&out);
    const auto choices = int(in.secp521_encrypted_private_key.has_value()) +
                         int(in.x448_encrypted_private_key.has_value()) + int(in.tpm_encrypted_private_key.has_value());
    if (choices > 1) {
        throw std::invalid_argument("SignedInstallationData requires one encrypted private key");
    }
    if (choices == 0) {
        // [V2G20-2202]: the mandatory choice still needs an empty placeholder.
        CB_SET_USED(out.SECP521_EncryptedPrivateKey);
    }
    CPP2CB_STRING(in.id, out.Id);
    convert(in.contract_certificate_chain, out.ContractCertificateChain);
    cb_convert_enum(in.ecdh_curve, out.ECDHCurve);
    CPP2CB_BYTES(in.dh_public_key, out.DHPublicKey);
    if (in.secp521_encrypted_private_key) {
        CB_SET_USED(out.SECP521_EncryptedPrivateKey);
        CPP2CB_BYTES(in.secp521_encrypted_private_key.value(), out.SECP521_EncryptedPrivateKey);
    }
    if (in.x448_encrypted_private_key) {
        CB_SET_USED(out.X448_EncryptedPrivateKey);
        CPP2CB_BYTES(in.x448_encrypted_private_key.value(), out.X448_EncryptedPrivateKey);
    }
    if (in.tpm_encrypted_private_key) {
        CB_SET_USED(out.TPM_EncryptedPrivateKey);
        CPP2CB_BYTES(in.tpm_encrypted_private_key.value(), out.TPM_EncryptedPrivateKey);
    }
}

} // namespace

template <> void convert(const struct iso20_CertificateInstallationReqType& in, CertificateInstallationRequest& out) {
    convert(in.Header, out.header);
    convert_chain(in.OEMProvisioningCertificateChain, out.oem_provisioning_certificate_chain);
    convert_root_ids(in.ListOfRootCertificateIDs, out.list_of_root_certificate_ids);
    out.maximum_contract_certificate_chains = in.MaximumContractCertificateChains;
    out.prioritized_emaids.reset();
    if (in.PrioritizedEMAIDs_isUsed) {
        auto& emaids = out.prioritized_emaids.emplace();
        for (uint16_t i = 0; i < in.PrioritizedEMAIDs.EMAID.arrayLen and i < emaids.capacity(); ++i) {
            emaids.emplace_back(CB2CPP_STRING(in.PrioritizedEMAIDs.EMAID.array[i]));
        }
    }
}

template <> void convert(const CertificateInstallationRequest& in, iso20_CertificateInstallationReqType& out) {
    init_iso20_CertificateInstallationReqType(&out);
    convert(in.header, out.Header);
    convert_chain(in.oem_provisioning_certificate_chain, out.OEMProvisioningCertificateChain);
    convert_root_ids(in.list_of_root_certificate_ids, out.ListOfRootCertificateIDs);
    out.MaximumContractCertificateChains = in.maximum_contract_certificate_chains;
    if (in.prioritized_emaids) {
        CB_SET_USED(out.PrioritizedEMAIDs);
        init_iso20_EMAIDListType(&out.PrioritizedEMAIDs);
        const auto& emaids = in.prioritized_emaids.value();
        CPP2CB_ARRAY_SIZE_CHECK(emaids.size(), out.PrioritizedEMAIDs.EMAID.array);
        out.PrioritizedEMAIDs.EMAID.arrayLen = static_cast<uint16_t>(emaids.size());
        for (std::size_t i = 0; i < emaids.size(); ++i) {
            CPP2CB_STRING(emaids[i], out.PrioritizedEMAIDs.EMAID.array[i]);
        }
    }
}

template <> void convert(const struct iso20_CertificateInstallationResType& in, CertificateInstallationResponse& out) {
    convert(in.Header, out.header);
    cb_convert_enum(in.ResponseCode, out.response_code);
    cb_convert_enum(in.EVSEProcessing, out.evse_processing);
    convert_chain(in.CPSCertificateChain, out.cps_certificate_chain);
    convert_installation_data(in.SignedInstallationData, out.signed_installation_data);
    out.remaining_contract_certificate_chains = in.RemainingContractCertificateChains;
}

template <> void convert(const CertificateInstallationResponse& in, iso20_CertificateInstallationResType& out) {
    init_iso20_CertificateInstallationResType(&out);
    convert(in.header, out.Header);
    cb_convert_enum(in.response_code, out.ResponseCode);
    cb_convert_enum(in.evse_processing, out.EVSEProcessing);
    convert_chain(in.cps_certificate_chain, out.CPSCertificateChain);
    convert_installation_data(in.signed_installation_data, out.SignedInstallationData);
    out.RemainingContractCertificateChains = in.remaining_contract_certificate_chains;
}

template <> void insert_type(VariantAccess& va, const struct iso20_CertificateInstallationReqType& in) {
    va.insert_type<CertificateInstallationRequest>(in);
};

template <> void insert_type(VariantAccess& va, const struct iso20_CertificateInstallationResType& in) {
    va.insert_type<CertificateInstallationResponse>(in);
};

template <> int serialize_to_exi(const CertificateInstallationResponse& in, exi_bitstream_t& out) {
    iso20_exiDocument doc{};
    init_iso20_exiDocument(&doc);

    CB_SET_USED(doc.CertificateInstallationRes);

    convert(in, doc.CertificateInstallationRes);

    return encode_iso20_exiDocument(&out, &doc);
}

template <> int serialize_to_exi(const CertificateInstallationRequest& in, exi_bitstream_t& out) {
    iso20_exiDocument doc{};
    init_iso20_exiDocument(&doc);

    CB_SET_USED(doc.CertificateInstallationReq);

    convert(in, doc.CertificateInstallationReq);

    return encode_iso20_exiDocument(&out, &doc);
}

template <> size_t serialize(const CertificateInstallationResponse& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

template <> size_t serialize(const CertificateInstallationRequest& in, const io::StreamOutputView& out) {
    return serialize_helper(in, out);
}

} // namespace iso15118::message_20
