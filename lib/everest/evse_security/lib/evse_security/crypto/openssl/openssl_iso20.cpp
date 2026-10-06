// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <evse_security/crypto/openssl/openssl_iso20.hpp>

#include <cstddef>
#include <string>

#include <openssl/asn1.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>

namespace evse_security::iso20 {

KeyType key_type(const EVP_PKEY* pkey) {
    if (pkey == nullptr) {
        return KeyType::None;
    }
    const int base = EVP_PKEY_base_id(pkey);
    if (base == EVP_PKEY_ED448) {
        return KeyType::Ed448;
    }
    if (base != EVP_PKEY_EC) {
        return KeyType::None;
    }
    char name[64] = {0};
    std::size_t len = 0;
    if (EVP_PKEY_get_group_name(pkey, name, sizeof(name), &len) != 1) {
        return KeyType::None;
    }
    const std::string group(name, len);
    return (group == "secp521r1" or group == "P-521") ? KeyType::Secp521r1 : KeyType::None;
}

int verify_annex_b_extensions(int preverified, X509_STORE_CTX* ctx) {
    if (preverified or X509_STORE_CTX_get_error(ctx) != X509_V_ERR_UNHANDLED_CRITICAL_EXTENSION) {
        return preverified;
    }
    X509* cert = X509_STORE_CTX_get_current_cert(ctx);
    for (int i = 0; i < X509_get_ext_count(cert); ++i) {
        X509_EXTENSION* ext = X509_get_ext(cert, i);
        if (not X509_EXTENSION_get_critical(ext) or X509_supported_extension(ext)) {
            continue;
        }
        const int nid = OBJ_obj2nid(X509_EXTENSION_get_object(ext));
        if (nid != NID_authority_key_identifier and nid != NID_subject_key_identifier and nid != NID_info_access and
            nid != NID_crl_distribution_points and nid != NID_sinfo_access) {
            return 0;
        }
        const X509V3_EXT_METHOD* method = X509V3_EXT_get(ext);
        void* decoded = X509V3_EXT_d2i(ext);
        if (decoded == nullptr or method == nullptr) {
            return 0;
        }
        if (method->it) {
            ASN1_item_free(static_cast<ASN1_VALUE*>(decoded), ASN1_ITEM_ptr(method->it));
        } else {
            method->ext_free(decoded);
        }
    }
    X509_STORE_CTX_set_error(ctx, X509_V_OK);
    return 1;
}

} // namespace evse_security::iso20
