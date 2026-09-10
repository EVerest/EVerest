// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/certificate_installation.hpp>
#include <iso15118/message_2/common_types.hpp>
#include <iso15118/message_2/metering_receipt.hpp>

// ISO 15118-2 Plug & Charge crypto, EVCC half only: signing and contract-key decryption. The SECC
// verification half (contract chain validation, AuthorizationReq/MeteringReceiptReq signature checks)
// lives on the charger side and is not part of the EV engine.
namespace iso15118::ev::d2::crypto {

// EC private key in PEM, optionally password-protected. No OpenSSL types leak into headers.
struct PrivateKey {
    std::string pem;
    std::optional<std::string> password{};
};

// Serialize and sign an AuthorizationReq / MeteringReceiptReq / CertificateInstallationReq. The request
// element is signed per [V2G2-771]: SHA-256 over the element EXI fragment, ECDSA-P256 over the
// SignedInfo, raw r||s; the iso2 Signature is attached to the message header. Returns the full V2G
// message EXI payload, or an empty vector on failure.
std::vector<uint8_t> serialize_signed(const message_2::AuthorizationRequest& req, const PrivateKey& key);
std::vector<uint8_t> serialize_signed(const message_2::MeteringReceiptRequest& req, const PrivateKey& key);
std::vector<uint8_t> serialize_signed(const message_2::CertificateInstallationRequest& req, const PrivateKey& key);

// Verify the CPS xmldsig signature carried in a CertificateInstallationRes header. `res_exi` is the raw
// response EXI (re-decoded here); the SAProvisioningCertificateChain leaf public key signs, and that
// chain is validated up to the trusted V2G root at `v2g_root_path`.
bool verify_certificate_installation_res(const std::vector<uint8_t>& res_exi, const std::string& v2g_root_path);

// Decrypt the ContractSignatureEncryptedPrivateKey [ISO 15118-2 7.9.2.4.3, V2G2-814..822].
// `encrypted_with_iv` is the 16-byte IV followed by the ciphertext; `dh_public_key` is the sender
// ephemeral EC public key (RFC 5480 uncompressed 0x04||X||Y). Returns the 32-byte scalar, or empty.
std::vector<uint8_t> decrypt_contract_private_key(const std::vector<uint8_t>& encrypted_with_iv,
                                                  const std::vector<uint8_t>& dh_public_key,
                                                  const PrivateKey& oem_priv_key);

// Build a PEM "EC PRIVATE KEY" from a raw 32-byte secp256r1 private scalar. The public point is
// recomputed from the scalar. Returns empty on failure.
std::string contract_scalar_to_pem(const std::vector<uint8_t>& scalar);

// Concatenate a DER leaf certificate and its DER sub-certificates into one PEM chain, leaf first.
std::string der_chain_to_pem(const std::vector<uint8_t>& leaf_der, const std::vector<std::vector<uint8_t>>& subs_der);

// eMAID of a contract leaf certificate (DER): its CommonName with '-' removed. Empty on failure.
std::string emaid_from_contract_der(const std::vector<uint8_t>& leaf_der);

// Parse a PEM certificate chain (leaf first, one or more PEM blocks) into DER certificates.
std::vector<std::vector<uint8_t>> pem_chain_to_der(const std::string& pem);

// ListOfRootCertificateIDs entry (X509 issuer DN + serial) of a root certificate (DER). The serial is
// truncated to int64, matching the ISO 15118-20 layer. issuer_name stays empty on failure.
message_2::RootCertificateId root_cert_id_from_der(const std::vector<uint8_t>& root_der);

} // namespace iso15118::ev::d2::crypto
