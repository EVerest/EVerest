// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

// Runtime generated ISO 15118-20 test PKI (root -> sub-CA1 -> sub-CA2 -> leaf) on secp521r1, Ed448 or, for
// the negative profile cases, prime256v1. The root goes to a temporary PEM bundle as the trust anchor.

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <cbv2g/iso_20/iso20_CommonMessages_Decoder.h>
#include <cbv2g/iso_20/iso20_CommonMessages_Encoder.h>

#include <iso15118/detail/d20/crypto.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/variant.hpp>

namespace iso15118::test_pki {

namespace dt = message_20::datatypes;
namespace crypto = d20::crypto;

using PKEY_ptr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using X509_ptr = std::unique_ptr<X509, decltype(&X509_free)>;

inline PKEY_ptr generate_key(const char* curve) {
    if (std::strcmp(curve, "ED448") == 0) {
        return PKEY_ptr(EVP_PKEY_Q_keygen(nullptr, nullptr, "ED448"), &EVP_PKEY_free);
    }
    return PKEY_ptr(EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", curve), &EVP_PKEY_free);
}

inline std::string key_to_pem(EVP_PKEY* key) {
    BIO* bio = BIO_new(BIO_s_mem());
    REQUIRE(PEM_write_bio_PrivateKey(bio, key, nullptr, nullptr, 0, nullptr, nullptr) == 1);
    char* data = nullptr;
    const long n = BIO_get_mem_data(bio, &data);
    std::string pem(data, static_cast<std::size_t>(n));
    BIO_free(bio);
    return pem;
}

inline std::vector<uint8_t> to_der(X509* cert) {
    unsigned char* der = nullptr;
    const int len = i2d_X509(cert, &der);
    REQUIRE(len > 0);
    std::vector<uint8_t> out(der, der + len);
    OPENSSL_free(der);
    return out;
}

inline std::string to_pem(X509* cert) {
    BIO* bio = BIO_new(BIO_s_mem());
    REQUIRE(PEM_write_bio_X509(bio, cert) == 1);
    char* data = nullptr;
    const long n = BIO_get_mem_data(bio, &data);
    std::string pem(data, static_cast<std::size_t>(n));
    BIO_free(bio);
    return pem;
}

struct CertSpec {
    std::string cn;
    std::string o{"EVerest"};
    bool ca{false};
    int path_len{-1}; // -1: no pathLenConstraint
    long not_before_offset_s{-3600};
    long not_after_offset_s{365L * 24 * 3600};
    std::vector<std::string> extra_ext{}; // "nid_name=value"
    bool omit_aia{false};
};

inline X509_ptr make_cert(const CertSpec& spec, EVP_PKEY* subject_key, X509* issuer, EVP_PKEY* issuer_key,
                          long serial) {
    X509_ptr cert(X509_new(), &X509_free);
    REQUIRE(X509_set_version(cert.get(), 2) == 1);
    ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), serial);
    X509_gmtime_adj(X509_getm_notBefore(cert.get()), spec.not_before_offset_s);
    X509_gmtime_adj(X509_getm_notAfter(cert.get()), spec.not_after_offset_s);
    REQUIRE(X509_set_pubkey(cert.get(), subject_key) == 1);

    X509_NAME* name = X509_get_subject_name(cert.get());
    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC, reinterpret_cast<const unsigned char*>(spec.o.c_str()), -1, -1,
                               0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>(spec.cn.c_str()), -1,
                               -1, 0);
    X509* issuer_cert = issuer != nullptr ? issuer : cert.get();
    REQUIRE(X509_set_issuer_name(cert.get(), X509_get_subject_name(issuer_cert)) == 1);

    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, issuer_cert, cert.get(), nullptr, nullptr, 0);
    auto add = [&](int nid, const std::string& value) {
        X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, &ctx, nid, value.c_str());
        REQUIRE(ext != nullptr);
        REQUIRE(X509_add_ext(cert.get(), ext, -1) == 1);
        X509_EXTENSION_free(ext);
    };
    std::string basic = spec.ca ? "critical,CA:TRUE" : "critical,CA:FALSE";
    if (spec.ca and spec.path_len >= 0) {
        basic += ",pathlen:" + std::to_string(spec.path_len);
    }
    add(NID_basic_constraints, basic);
    add(NID_key_usage, spec.ca ? "critical,keyCertSign,cRLSign" : "critical,digitalSignature");
    add(NID_subject_key_identifier, "critical,hash");
    if (issuer != nullptr) {
        add(NID_authority_key_identifier, "critical,keyid:always");
    }
    if (not spec.omit_aia) {
        add(NID_info_access, "critical,OCSP;URI:http://ocsp.example.org");
    }
    for (const auto& extra : spec.extra_ext) {
        const auto eq = extra.find('=');
        add(OBJ_txt2nid(extra.substr(0, eq).c_str()), extra.substr(eq + 1));
    }

    const EVP_MD* md = (EVP_PKEY_base_id(issuer_key) == EVP_PKEY_ED448) ? nullptr : EVP_sha512();
    REQUIRE(X509_sign(cert.get(), issuer_key, md) > 0);
    return cert;
}

// A three-level eMSP PKI (root -> sub-CA1 -> sub-CA2 -> contract leaf) on one curve.
struct Pki {
    PKEY_ptr root_key{nullptr, &EVP_PKEY_free};
    PKEY_ptr sub1_key{nullptr, &EVP_PKEY_free};
    PKEY_ptr sub2_key{nullptr, &EVP_PKEY_free};
    PKEY_ptr leaf_key{nullptr, &EVP_PKEY_free};
    X509_ptr root{nullptr, &X509_free};
    X509_ptr sub1{nullptr, &X509_free};
    X509_ptr sub2{nullptr, &X509_free};
    X509_ptr leaf{nullptr, &X509_free};
    std::string root_bundle_path;

    Pki() = default;
    Pki(Pki&& other) noexcept :
        root_key(std::move(other.root_key)),
        sub1_key(std::move(other.sub1_key)),
        sub2_key(std::move(other.sub2_key)),
        leaf_key(std::move(other.leaf_key)),
        root(std::move(other.root)),
        sub1(std::move(other.sub1)),
        sub2(std::move(other.sub2)),
        leaf(std::move(other.leaf)),
        root_bundle_path(std::move(other.root_bundle_path)) {
        other.root_bundle_path.clear();
    }
    Pki& operator=(Pki&&) = delete;

    ~Pki() {
        if (not root_bundle_path.empty()) {
            std::remove(root_bundle_path.c_str());
        }
    }

    std::vector<uint8_t> leaf_der() const {
        return to_der(leaf.get());
    }
    std::vector<std::vector<uint8_t>> subs_der() const {
        return {to_der(sub2.get()), to_der(sub1.get())};
    }
    crypto::PrivateKey leaf_private_key() const {
        return crypto::PrivateKey{key_to_pem(leaf_key.get()), std::nullopt};
    }
};

inline Pki make_pki(const char* curve, CertSpec leaf_spec = {"DE-ABC-C123ABC56-X"},
                    CertSpec sub1_spec = {"eMSP Sub-CA1"}, CertSpec sub2_spec = {"eMSP Sub-CA2"}) {
    Pki pki;
    pki.root_key = generate_key(curve);
    pki.sub1_key = generate_key(curve);
    pki.sub2_key = generate_key(curve);
    pki.leaf_key = generate_key(curve);
    REQUIRE(pki.leaf_key != nullptr);

    CertSpec root_spec{"eMSP Root CA"};
    root_spec.ca = true;
    root_spec.omit_aia = true;
    pki.root = make_cert(root_spec, pki.root_key.get(), nullptr, pki.root_key.get(), 1);
    sub1_spec.ca = true;
    sub1_spec.path_len = 1;
    pki.sub1 = make_cert(sub1_spec, pki.sub1_key.get(), pki.root.get(), pki.root_key.get(), 2);
    sub2_spec.ca = true;
    sub2_spec.path_len = 0;
    pki.sub2 = make_cert(sub2_spec, pki.sub2_key.get(), pki.sub1.get(), pki.sub1_key.get(), 3);
    pki.leaf = make_cert(leaf_spec, pki.leaf_key.get(), pki.sub2.get(), pki.sub2_key.get(), 4);

    char path[] = "/tmp/iso20_pnc_root_XXXXXX";
    const int fd = mkstemp(path);
    REQUIRE(fd >= 0);
    const auto pem = to_pem(pki.root.get());
    REQUIRE(write(fd, pem.data(), pem.size()) == static_cast<ssize_t>(pem.size()));
    close(fd);
    pki.root_bundle_path = path;
    return pki;
}

inline std::vector<uint8_t> unsigned_authorization_req(const Pki& pki, const dt::GenChallenge& challenge) {
    message_20::AuthorizationRequest req;
    req.header.session_id = {1, 2, 3, 4, 5, 6, 7, 8};
    req.header.timestamp = 1691411798;
    req.selected_authorization_service = dt::Authorization::PnC;
    auto& pnc = req.authorization_mode.emplace<dt::PnC_ASReqAuthorizationMode>();
    pnc.id = "id1";
    pnc.gen_challenge = challenge;
    pnc.contract_certificate_chain.certificate = pki.leaf_der();
    for (const auto& sub : pki.subs_der()) {
        pnc.contract_certificate_chain.sub_certificates.emplace_back(sub);
    }
    std::vector<uint8_t> buffer(16384);
    io::StreamOutputView out({buffer.data(), buffer.size()});
    buffer.resize(message_20::serialize(req, out));
    return buffer;
}

// Re-encodes a signed request after letting the caller mutate the cbexigen document.
template <typename Mutator>
inline std::vector<uint8_t> mutate_document(const std::vector<uint8_t>& exi, Mutator mutate) {
    auto doc = std::make_unique<iso20_exiDocument>();
    exi_bitstream_t in;
    exi_bitstream_init(&in, const_cast<uint8_t*>(exi.data()), exi.size(), 0, nullptr);
    REQUIRE(decode_iso20_exiDocument(&in, doc.get()) == 0);
    mutate(*doc);
    std::vector<uint8_t> buffer(16384);
    exi_bitstream_t out;
    exi_bitstream_init(&out, buffer.data(), buffer.size(), 0, nullptr);
    REQUIRE(encode_iso20_exiDocument(&out, doc.get()) == 0);
    buffer.resize(exi_bitstream_get_length(&out));
    return buffer;
}

} // namespace iso15118::test_pki
