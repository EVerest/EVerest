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

// ISO 15118-2 Plug-and-Charge (PnC) crypto for the d2 engine. The SECC (verify) side was ported from the
// EvseV2G reference (modules/EVSE/EvseV2G/crypto/crypto_openssl.cpp): contract chain validation + xmldsig
// verification over the signed AuthorizationReq/MeteringReceiptReq EXI fragment. The EVCC (sign +
// contract-key decrypt) side is the inverse, implemented fresh against ISO 15118-2 §7.9.2.4.3 / Annex G.
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
    // Set when the failure is only a missing local trust anchor (MO root absent / issuer not found):
    // with central contract validation allowed the SECC may accept the chain and forward it to the
    // CSMS for validation instead (EvseV2G parity). emaid/chain_pem are filled in this case too.
    bool forwardable{false};
};

// Parse the contract leaf certificate + SubCertificates, verify the chain to the trusted MO/V2G root(s)
// and cross-check the requested eMAID against the leaf CommonName [V2G2-...]. Returns OK with the eMAID
// and PEM chain, or the appropriate FAILED_* response code.
ContractValidationResult validate_contract_chain(const std::vector<uint8_t>& leaf_der,
                                                 const std::vector<std::vector<uint8_t>>& sub_certs,
                                                 const std::string& req_emaid, const std::string& mo_root_path,
                                                 const std::string& v2g_root_path);

// Verify the ISO 15118-2 xmldsig signature carried in the message header over the signed AuthorizationReq
// element. `request_exi` is the raw EXI of the received V2G message (re-decoded here to recover the cbv2g
// iso2 structs); the contract public key is taken from `leaf_der`. Returns true when the digest matches
// and the ECDSA-P256 signature verifies.
bool verify_authorization_signature(const std::vector<uint8_t>& request_exi, const std::vector<uint8_t>& leaf_der);

// Verify the ISO 15118-2 xmldsig signature carried in the message header over the signed
// MeteringReceiptReq element. Same scheme as verify_authorization_signature (contract-key ECDSA-P256
// over the SignedInfo, digest over the signed element fragment) but the signed element is the whole
// MeteringReceiptReq. `request_exi` is the raw EXI of the received message; the contract public key is
// taken from `leaf_der`.
bool verify_metering_receipt_signature(const std::vector<uint8_t>& request_exi, const std::vector<uint8_t>& leaf_der);

// --- EVCC (client) side: signing + contract-key decryption ---

// Serialize and sign an AuthorizationReq / MeteringReceiptReq / CertificateInstallationReq. The request
// element is signed with `key` (xmldsig per [V2G2-771]: SHA-256 over the element EXI fragment, ECDSA-P256
// over the SignedInfo, raw r||s), and the resulting iso2 Signature is attached to the message header.
// Returns the full V2G message EXI payload, or an empty vector on failure.
std::vector<uint8_t> serialize_signed(const message_2::AuthorizationRequest& req, const PrivateKey& key);
std::vector<uint8_t> serialize_signed(const message_2::MeteringReceiptRequest& req, const PrivateKey& key);
std::vector<uint8_t> serialize_signed(const message_2::CertificateInstallationRequest& req, const PrivateKey& key);

// Verify the CPS xmldsig signature carried in a CertificateInstallationRes header over its four signed
// elements (ContractSignatureCertChain, ContractSignatureEncryptedPrivateKey, DHpublickey, eMAID).
// `res_exi` is the raw response EXI (re-decoded here); the SAProvisioningCertificateChain leaf public key
// is used and validated up to the trusted V2G root at `v2g_root_path`. Returns true when every reference
// digest matches and the ECDSA-P256 signature verifies.
bool verify_certificate_installation_res(const std::vector<uint8_t>& res_exi, const std::string& v2g_root_path);

// Decrypt the ISO 15118-2 ContractSignatureEncryptedPrivateKey [§7.9.2.4.3 / V2G2-814..822]:
// ephemeral-static ECDH on secp256r1 (receiver key = the OEM provisioning private key), ConcatKDF-SHA256
// (OtherInfo = 0x01 0x55 0x56, 16-byte session key), AES-128-CBC decrypt. `encrypted_with_iv` is the
// 16-byte IV (MSBs) followed by the ciphertext; `dh_public_key` is the sender's ephemeral EC public key
// (RFC 5480 uncompressed 0x04||X||Y). Returns the raw 32-byte secp256r1 contract private scalar, or empty.
std::vector<uint8_t> decrypt_contract_private_key(const std::vector<uint8_t>& encrypted_with_iv,
                                                  const std::vector<uint8_t>& dh_public_key,
                                                  const PrivateKey& oem_priv_key);

// Build a PEM "EC PRIVATE KEY" from a raw 32-byte secp256r1 private scalar (the decrypted contract key).
// The public point is recomputed from the scalar. Returns empty on failure.
std::string contract_scalar_to_pem(const std::vector<uint8_t>& scalar);

// Concatenate a DER leaf certificate and its DER sub-certificates into a single PEM chain (leaf first).
// Returns empty on failure.
std::string der_chain_to_pem(const std::vector<uint8_t>& leaf_der, const std::vector<std::vector<uint8_t>>& subs_der);

// Extract the eMAID from a contract leaf certificate (DER): its CommonName with '-' characters removed.
// Returns empty on failure. Used by the module to fill PaymentDetailsReq.eMAID from a configured contract
// certificate so it matches the leaf CN the SECC cross-checks.
std::string emaid_from_contract_der(const std::vector<uint8_t>& leaf_der);

// Parse a PEM certificate chain (leaf first, one or more PEM blocks) into DER certificates. Returns an
// empty vector on failure. Used by the module to load configured contract/OEM certificate files.
std::vector<std::vector<uint8_t>> pem_chain_to_der(const std::string& pem);

// Extract the ListOfRootCertificateIDs entry (X509 issuer DN + serial number) from a root certificate
// (DER). The serial is truncated to int64 (a known simplification, matching the ISO 15118-20 layer).
// Returns a RootCertificateId with an empty issuer_name on failure.
message_2::RootCertificateId root_cert_id_from_der(const std::vector<uint8_t>& root_der);

} // namespace iso15118::d2::crypto
