// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "common_types.hpp"

namespace iso15118::message_2 {

// Widest serial the EXI integer converter can take. It writes ceil(8 * n / 7) octets into a fixed
// 29-octet buffer and checks no length itself, so 26 bytes already overrun it while still reporting
// success.
inline constexpr std::size_t MAX_SERIAL_NUMBER_BYTES = 25;

// One entry of the CertificateInstallationReq ListOfRootCertificateIDs (X509IssuerSerialType). The
// serial is the big-endian magnitude as carried in the certificate; RFC 5280 allows up to 20 octets,
// which no integer type of the C++ layer holds.
struct RootCertificateId {
    std::string issuer_name;
    std::vector<uint8_t> serial_number;
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

// CertificateUpdateReq/Res (8.4.3.10). The SECC relays a successful exchange verbatim; these types
// exist so the SECC can build the FAILED_* responses it owes itself ([V2G2-538], [V2G2-460],
// [V2G2-558]) and so tests / an EV side can drive the request. The signature over the request
// element (contract leaf key) is attached at serialization time like for the installation request.
struct CertificateUpdateRequest {
    Header header;
    std::string id{"id1"};
    CertificateChain contract_chain;
    std::string emaid;
    std::vector<RootCertificateId> root_certificate_ids;
};

struct CertificateUpdateResponse {
    Header header;
    datatypes::ResponseCode response_code{datatypes::ResponseCode::FAILED};
    CertificateChain sa_provisioning_chain;
    CertificateChain contract_chain;
    std::vector<uint8_t> encrypted_private_key;
    std::vector<uint8_t> dh_public_key;
    std::string emaid;
    // RetryCounter: hint for the EVCC when to retry a FAILED_NoCertificateAvailable ([V2G2-696]).
    std::optional<int16_t> retry_counter;
};

} // namespace iso15118::message_2
