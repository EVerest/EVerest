// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "common_types.hpp"

namespace iso15118::message_2 {

// The serial is modelled as int64 for parity with the ISO 15118-20 layer; real X509 serials may
// exceed 64 bit, which is a known simplification.
struct RootCertificateId {
    std::string issuer_name;
    int64_t serial_number{0};
};

// The whole element is signed with the OEM provisioning certificate key and the signature is
// attached to the message header at serialization time, so it is not modelled here.
struct CertificateInstallationRequest {
    Header header;
    std::string id{"id1"};
    // Its public key is the ECDH receiver key the backend encrypts the contract private key to.
    std::vector<uint8_t> oem_provisioning_cert;
    std::vector<RootCertificateId> root_certificate_ids;
};

struct CertificateChain {
    std::optional<std::string> id;
    std::vector<uint8_t> certificate;
    std::vector<std::vector<uint8_t>> sub_certificates;
};

// The CPS signature over the four signed elements is verified over the raw response EXI.
struct CertificateInstallationResponse {
    Header header;
    datatypes::ResponseCode response_code{datatypes::ResponseCode::FAILED};
    CertificateChain sa_provisioning_chain;
    CertificateChain contract_chain;
    // ContractSignatureEncryptedPrivateKey: 16-byte IV (MSBs) followed by the AES-128-CBC ciphertext.
    std::vector<uint8_t> encrypted_private_key;
    // DHpublickey: RFC 5480 uncompressed EC point (0x04 || X || Y).
    std::vector<uint8_t> dh_public_key;
    std::string emaid;
};

} // namespace iso15118::message_2
