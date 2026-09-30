// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <iso15118/message/common_types.hpp>

// ISO 15118-20 application layer security on the SECC: xmldsig verification per 7.9.2 and the local
// contract chain checks per [V2G20-1001] and Annex B. No OpenSSL types leak out.
namespace iso15118::d20::crypto {

namespace dt = message_20::datatypes;

struct PrivateKey {
    std::string pem;
    std::optional<std::string> password{};
};

struct ContractValidationResult {
    // OK on success, otherwise the WARNING_* code for the AuthorizationRes ([V2G20-2211..2215]).
    dt::ResponseCode response_code{dt::ResponseCode::WARNING_CertificateValidationError};
    // eMAID (CommonName) of the contract leaf, '-' characters removed.
    std::string emaid;
    // Leaf and sub-CA certificates in PEM, for the require_auth_pnc token.
    std::string chain_pem;
    // Only the trust anchor is missing locally: with central contract validation allowed the chain
    // goes to the backend instead. emaid/chain_pem are filled too.
    bool forwardable{false};
    // [V2G20-2218]: the leaf expires within 14 days.
    bool expires_within_14_days{false};
};

// Validity of every element, the Annex B profile and the trust chain against the MO or V2G roots at the
// given paths. Validity faults are reported before profile faults, so the EV learns the actual defect.
ContractValidationResult validate_contract_chain(const std::vector<uint8_t>& leaf_der,
                                                 const std::vector<std::vector<uint8_t>>& sub_certs,
                                                 const std::string& mo_root_path, const std::string& v2g_root_path);

// Time validity of every element of a chain: WARNING_CertificateExpired or WARNING_CertificateNotYetValid
// ([V2G20-2203], [V2G20-2204] for the OEM provisioning chain, [V2G20-2212], [V2G20-2213] for the contract
// chain), WARNING_CertificateValidationError when a certificate does not parse, nothing when the whole
// chain is valid. Trust is not checked.
std::optional<dt::ResponseCode> chain_validity_fault(const std::vector<uint8_t>& leaf_der,
                                                     const std::vector<std::vector<uint8_t>>& sub_certs);

enum class SignedElement {
    PnC_AReqAuthorizationMode,  // AuthorizationReq, signed with the contract leaf key [V2G20-1063]
    CertificateInstallationReq, // OEMProvisioningCertificateChain, the request's only Id-carrying element,
                                // signed with the OEM provisioning leaf key [V2G20-1548]
};

// [V2G20-1582]: a repeated AuthorizationReq may change only its timestamp. The header signature is left
// out as well: every request's signature is verified on its own, and an ECDSA signature over the same
// element differs on every signing.
std::vector<uint8_t> authorization_request_without_timestamp_and_signature(const std::vector<uint8_t>& exi);

enum class SignatureVerdict {
    Ok,
    DecodeError,
    NoSignature,
    UnsupportedAlgorithm,
    DigestMismatch,
    SignatureInvalid,
};

// Rebuilds the EXI fragment of the signed element and checks the Reference digest and SignatureValue
// against the leaf key. ecdsa-sha512/SHA-512 or Ed448/SHAKE256 ([V2G20-2473..2476]), r||s per [V2G20-1000].
SignatureVerdict verify_signature(const std::vector<uint8_t>& request_exi, const std::vector<uint8_t>& leaf_der,
                                  SignedElement element);

// Attaches a header signature to an encoded, unsigned request: the inverse of verify_signature. The
// algorithms follow the key type (EC -> ecdsa-sha512/sha512, Ed448 -> Ed448/SHAKE256). Empty on failure.
std::vector<uint8_t> sign_document(const std::vector<uint8_t>& unsigned_request_exi, SignedElement element,
                                   const std::string& reference_id, const PrivateKey& key);

} // namespace iso15118::d20::crypto
