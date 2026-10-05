// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

// ISO 15118-20 Annex B rules that both the OCPP-side certificate validation and the SECC's own contract
// chain checks apply. One implementation keeps the two from drifting apart.

struct evp_pkey_st;
struct x509_store_ctx_st;

namespace evse_security::iso20 {

/// The key types Annex B admits for the V2G PKI.
enum class KeyType {
    None, ///< neither of the Annex B key types, for instance the prime256v1 of ISO 15118-2
    Secp521r1,
    Ed448,
};

KeyType key_type(const evp_pkey_st* pkey);

/// X509_STORE_CTX verify callback. Annex B marks the key identifiers and revocation pointers critical, which
/// IETF RFC 5280 does not, so OpenSSL rejects a conforming certificate as carrying an unhandled critical
/// extension. Those are accepted once they decode; any other unhandled critical extension stays fatal.
int verify_annex_b_extensions(int preverified, x509_store_ctx_st* ctx);

} // namespace evse_security::iso20
