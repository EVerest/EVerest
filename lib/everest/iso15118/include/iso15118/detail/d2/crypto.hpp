// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <iso15118/message_2/common_types.hpp>

// ISO 15118-2 Plug-and-Charge (PnC) crypto for the SECC (d2) engine. Ported from the EvseV2G reference
// (modules/EVSE/EvseV2G/crypto/crypto_openssl.cpp): contract certificate chain validation and the
// xmldsig signature verification over the signed AuthorizationReq EXI fragment.
namespace iso15118::d2::crypto {

namespace dt = message_2::datatypes;

struct ContractValidationResult {
    // OK on success, otherwise the ISO 15118-2 FAILED_* code to return in PaymentDetailsRes.
    dt::ResponseCode response_code{dt::ResponseCode::FAILED_CertChainError};
    // eMAID (CommonName) read from the contract leaf certificate ('-' characters removed).
    std::string emaid;
    // Contract leaf + sub-CA certificates concatenated in PEM form (for the require_auth_pnc token).
    std::string chain_pem;
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
bool verify_metering_receipt_signature(const std::vector<uint8_t>& request_exi,
                                       const std::vector<uint8_t>& leaf_der);

} // namespace iso15118::d2::crypto
