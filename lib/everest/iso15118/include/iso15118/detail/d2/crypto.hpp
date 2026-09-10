// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/certificate_installation.hpp>
#include <iso15118/message_2/common_types.hpp>
#include <iso15118/message_2/metering_receipt.hpp>

// The SECC (verify) side was ported from EvseV2G's crypto_openssl.cpp; the EVCC (sign + contract-key
// decrypt) side is the inverse, implemented fresh against ISO 15118-2 section 7.9.2.4.3 / Annex G.
namespace iso15118::d2::crypto {

namespace dt = message_2::datatypes;

// Opaque EC private key holder (PEM, optionally password-protected). No OpenSSL types leak into headers.
struct PrivateKey {
    std::string pem;
    std::optional<std::string> password{};
};

struct ContractValidationResult {
    // OK on success, otherwise the ISO 15118-2 FAILED_* code to return in PaymentDetailsRes.
    dt::ResponseCode response_code{dt::ResponseCode::FAILED_CertChainError};
    // eMAID (CommonName) read from the contract leaf certificate ('-' characters removed).
    std::string emaid;
    // Contract leaf + sub-CA certificates concatenated in PEM form (for the require_auth_pnc token).
    std::string chain_pem;
    // Set when the failure is only a missing local trust anchor: with central contract validation
    // allowed the SECC may forward the chain to the CSMS instead. emaid/chain_pem are filled too.
    bool forwardable{false};
};

ContractValidationResult validate_contract_chain(const std::vector<uint8_t>& leaf_der,
                                                 const std::vector<std::vector<uint8_t>>& sub_certs,
                                                 const std::string& req_emaid, const std::string& mo_root_path,
                                                 const std::string& v2g_root_path);

bool verify_authorization_signature(const std::vector<uint8_t>& request_exi, const std::vector<uint8_t>& leaf_der);

bool verify_metering_receipt_signature(const std::vector<uint8_t>& request_exi, const std::vector<uint8_t>& leaf_der);

// xmldsig per [V2G2-771]: SHA-256 over the element EXI fragment, ECDSA-P256 over the SignedInfo,
// raw r||s, attached to the message header.
std::vector<uint8_t> serialize_signed(const message_2::AuthorizationRequest& req, const PrivateKey& key);
std::vector<uint8_t> serialize_signed(const message_2::MeteringReceiptRequest& req, const PrivateKey& key);
std::vector<uint8_t> serialize_signed(const message_2::CertificateInstallationRequest& req, const PrivateKey& key);

// The SAProvisioningCertificateChain leaf is validated up to the trusted V2G root at v2g_root_path.
bool verify_certificate_installation_res(const std::vector<uint8_t>& res_exi, const std::string& v2g_root_path);

// [section 7.9.2.4.3 / V2G2-814..822]. \p encrypted_with_iv is the 16-byte IV (MSBs) followed by the
// ciphertext; \p dh_public_key is an RFC 5480 uncompressed point (0x04||X||Y).
std::vector<uint8_t> decrypt_contract_private_key(const std::vector<uint8_t>& encrypted_with_iv,
                                                  const std::vector<uint8_t>& dh_public_key,
                                                  const PrivateKey& oem_priv_key);

std::string contract_scalar_to_pem(const std::vector<uint8_t>& scalar);

std::string der_chain_to_pem(const std::vector<uint8_t>& leaf_der, const std::vector<std::vector<uint8_t>>& subs_der);

// CommonName with '-' removed, so it matches the leaf CN the SECC cross-checks.
std::string emaid_from_contract_der(const std::vector<uint8_t>& leaf_der);

std::vector<std::vector<uint8_t>> pem_chain_to_der(const std::string& pem);

// The serial is truncated to int64 -- a known simplification, matching the ISO 15118-20 layer.
message_2::RootCertificateId root_cert_id_from_der(const std::vector<uint8_t>& root_der);

} // namespace iso15118::d2::crypto
