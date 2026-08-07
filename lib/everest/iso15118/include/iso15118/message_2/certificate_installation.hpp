// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "common_types.hpp"

namespace iso15118::message_2 {

// One entry of the CertificateInstallationReq ListOfRootCertificateIDs (X509IssuerSerialType). The
// serial number is modelled as int64 for parity with the ISO 15118-20 layer (message/common_types.hpp);
// real X509 serials may exceed 64 bit, which is a known simplification.
struct RootCertificateId {
    std::string issuer_name;
    int64_t serial_number{0};
};

// ISO 15118-2 CertificateInstallationReq [V2G2-...]. The whole element is signed (xmldsig, referenced by
// its Id attribute) with the OEM provisioning certificate key; the signature is attached to the message
// header at serialization time (see d2::crypto::serialize_signed), so it is not modelled here.
struct CertificateInstallationRequest {
    Header header;
    // Id attribute of the signed CertificateInstallationReq element (referenced by the SignedInfo URI).
    std::string id{"id1"};
    // OEM provisioning certificate (DER) previously installed by the OEM; its public key is used by the
    // backend to encrypt the delivered contract private key (ECDH receiver key QV).
    std::vector<uint8_t> oem_provisioning_cert;
    // The root certificates currently installed in the EVCC (at least one is mandatory).
    std::vector<RootCertificateId> root_certificate_ids;
};

// A decoded CertificateChainType (leaf certificate DER + ordered SubCertificates, leaf-nearest first).
struct CertificateChain {
    std::optional<std::string> id;
    std::vector<uint8_t> certificate;
    std::vector<std::vector<uint8_t>> sub_certificates;
};

// ISO 15118-2 CertificateInstallationRes. Carries the new contract certificate chain, the encrypted
// contract private key (16-byte IV prepended), the sender's ephemeral ECDH public key and the eMAID.
// The whole response is signed by the CPS over the four signed elements (verified via
// d2::crypto::verify_certificate_installation_res over the raw response EXI).
struct CertificateInstallationResponse {
    Header header;
    datatypes::ResponseCode response_code{datatypes::ResponseCode::FAILED};
    // Chain used to verify the CPS signature carried in the message header.
    CertificateChain sa_provisioning_chain;
    // The contract certificate chain to install.
    CertificateChain contract_chain;
    // ContractSignatureEncryptedPrivateKey: 16-byte IV (MSBs) followed by the AES-128-CBC ciphertext.
    std::vector<uint8_t> encrypted_private_key;
    // DHpublickey: RFC 5480 uncompressed EC point (0x04 || X || Y).
    std::vector<uint8_t> dh_public_key;
    std::string emaid;
};

} // namespace iso15118::message_2
