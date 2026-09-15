// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <iso15118/detail/d2/crypto.hpp>
#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/certificate_installation.hpp>
#include <iso15118/message_2/common_types.hpp>
#include <iso15118/message_2/metering_receipt.hpp>

// ISO 15118-2 Plug & Charge crypto, EVCC half only: signing and contract-key decryption. The helpers
// that behave as the EV needs come from iso15118::d2::crypto; the functions declared here either differ
// from theirs in what they check or wipe, or are needed by the EV alone.
namespace iso15118::ev::d2::crypto {

using iso15118::d2::crypto::contract_scalar_to_pem;
using iso15118::d2::crypto::emaid_from_contract_der;
using iso15118::d2::crypto::PrivateKey;

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

// Wipe key material in place. std::vector's deallocation does not clear, and a plain memset over
// a dying buffer is a dead store the compiler may remove.
void wipe(std::vector<uint8_t>& buffer);

// Decrypt the ContractSignatureEncryptedPrivateKey [ISO 15118-2 7.9.2.4.3, V2G2-814..822].
// `encrypted_with_iv` is the 16-byte IV followed by the ciphertext; `dh_public_key` is the sender
// ephemeral EC public key (RFC 5480 uncompressed 0x04||X||Y). Returns the 32-byte scalar, or empty.
// Its result is that of iso15118::d2::crypto::decrypt_contract_private_key; the difference is memory
// hygiene only: the shared secret, the session key and a rejected plaintext are cleansed.
std::vector<uint8_t> decrypt_contract_private_key(const std::vector<uint8_t>& encrypted_with_iv,
                                                  const std::vector<uint8_t>& dh_public_key,
                                                  const PrivateKey& oem_priv_key);

// ListOfRootCertificateIDs entry (X509 issuer DN + serial) of a root certificate (DER). The serial is
// the big-endian magnitude. issuer_name stays empty on failure, including a serial too wide to encode.
message_2::RootCertificateId root_cert_id_from_der(const std::vector<uint8_t>& root_der);

std::string der_chain_to_pem(const std::vector<uint8_t>& leaf_der, const std::vector<std::vector<uint8_t>>& subs_der);

std::vector<std::vector<uint8_t>> pem_chain_to_der(const std::string& pem);

} // namespace iso15118::ev::d2::crypto
