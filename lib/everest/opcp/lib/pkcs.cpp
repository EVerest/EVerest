// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <everest/opcp/pkcs.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <memory>

#include <openssl/asn1.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/pkcs7.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <evse_security/evse_security.hpp>

namespace opcp {

namespace {

constexpr const char* PEM_CERT_BEGIN = "-----BEGIN CERTIFICATE-----";
constexpr const char* PEM_CERT_END = "-----END CERTIFICATE-----";
constexpr const char* PEM_CSR_BEGIN = "-----BEGIN CERTIFICATE REQUEST-----";
constexpr const char* PEM_CSR_END = "-----END CERTIFICATE REQUEST-----";

struct BioDeleter {
    void operator()(BIO* bio) const {
        BIO_free(bio);
    }
};
struct X509Deleter {
    void operator()(X509* x) const {
        X509_free(x);
    }
};
struct X509ReqDeleter {
    void operator()(X509_REQ* r) const {
        X509_REQ_free(r);
    }
};
struct Pkcs7Deleter {
    void operator()(PKCS7* p) const {
        PKCS7_free(p);
    }
};
using BioPtr = std::unique_ptr<BIO, BioDeleter>;
using X509Ptr = std::unique_ptr<X509, X509Deleter>;
using X509ReqPtr = std::unique_ptr<X509_REQ, X509ReqDeleter>;
using Pkcs7Ptr = std::unique_ptr<PKCS7, Pkcs7Deleter>;

std::string x509_to_pem(X509* cert) {
    const BioPtr bio(BIO_new(BIO_s_mem()));
    if (!bio || PEM_write_bio_X509(bio.get(), cert) != 1) {
        return {};
    }
    char* data = nullptr;
    const long length = BIO_get_mem_data(bio.get(), &data);
    return length > 0 ? std::string(data, static_cast<std::size_t>(length)) : std::string{};
}

X509Ptr pem_to_x509(const std::string& pem) {
    const BioPtr bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())));
    if (!bio) {
        return nullptr;
    }
    return X509Ptr(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
}

std::vector<X509Ptr> pem_to_x509_list(const std::string& pem) {
    std::vector<X509Ptr> out;
    const BioPtr bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())));
    if (!bio) {
        return out;
    }
    while (X509* cert = PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr)) {
        out.emplace_back(cert);
    }
    return out;
}

std::string name_to_string(const X509_NAME* name) {
    if (name == nullptr) {
        return {};
    }
    const BioPtr bio(BIO_new(BIO_s_mem()));
    if (!bio) {
        return {};
    }
    X509_NAME_print_ex(bio.get(), name, 0, XN_FLAG_RFC2253);
    char* data = nullptr;
    const long length = BIO_get_mem_data(bio.get(), &data);
    return length > 0 ? std::string(data, static_cast<std::size_t>(length)) : std::string{};
}

std::string asn1_time_to_string(const ASN1_TIME* time) {
    if (time == nullptr) {
        return {};
    }
    std::tm tm{};
    if (ASN1_TIME_to_tm(time, &tm) != 1) {
        return {};
    }
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

std::string common_name_of(X509* cert) {
    X509_NAME* name = X509_get_subject_name(cert);
    const int index = X509_NAME_get_index_by_NID(name, NID_commonName, -1);
    if (index < 0) {
        return {};
    }
    const ASN1_STRING* value = X509_NAME_ENTRY_get_data(X509_NAME_get_entry(name, index));
    unsigned char* utf8 = nullptr;
    const int length = ASN1_STRING_to_UTF8(&utf8, value);
    if (length < 0 || utf8 == nullptr) {
        return {};
    }
    std::string out(reinterpret_cast<char*>(utf8), static_cast<std::size_t>(length));
    OPENSSL_free(utf8);
    return out;
}

std::string public_key_algorithm_of(X509* cert) {
    EVP_PKEY* key = X509_get0_pubkey(cert);
    if (key == nullptr) {
        return {};
    }
    if (EVP_PKEY_base_id(key) == EVP_PKEY_EC) {
        char curve[64] = {};
        std::size_t length = 0;
        if (EVP_PKEY_get_utf8_string_param(key, OSSL_PKEY_PARAM_GROUP_NAME, curve, sizeof(curve), &length) == 1) {
            return curve;
        }
    }
    const char* name = OBJ_nid2sn(EVP_PKEY_base_id(key));
    return name != nullptr ? name : "";
}

bool is_self_signed(X509* cert) {
    return X509_NAME_cmp(X509_get_subject_name(cert), X509_get_issuer_name(cert)) == 0 &&
           X509_verify(cert, X509_get0_pubkey(cert)) == 1;
}

std::vector<std::string> pkcs7_der_to_pem(const std::vector<std::uint8_t>& der) {
    std::vector<std::string> out;
    const unsigned char* data = der.data();
    const Pkcs7Ptr p7(d2i_PKCS7(nullptr, &data, static_cast<long>(der.size())));
    if (!p7) {
        return out;
    }
    STACK_OF(X509)* certs = nullptr;
    const int type = OBJ_obj2nid(p7->type);
    if (type == NID_pkcs7_signed && p7->d.sign != nullptr) {
        certs = p7->d.sign->cert;
    } else if (type == NID_pkcs7_signedAndEnveloped && p7->d.signed_and_enveloped != nullptr) {
        certs = p7->d.signed_and_enveloped->cert;
    }
    if (certs == nullptr) {
        return out;
    }
    for (int i = 0; i < sk_X509_num(certs); ++i) {
        std::string pem = x509_to_pem(sk_X509_value(certs, i));
        if (!pem.empty()) {
            out.push_back(std::move(pem));
        }
    }
    return out;
}

} // namespace

std::string strip_whitespace(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (const char c : input) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            out.push_back(c);
        }
    }
    return out;
}

std::string pem_csr_to_base64_der(const std::string& csr_pem) {
    const auto begin = csr_pem.find(PEM_CSR_BEGIN);
    const auto end = csr_pem.find(PEM_CSR_END);
    if (begin == std::string::npos || end == std::string::npos || end <= begin) {
        return {};
    }
    const auto body_start = begin + std::strlen(PEM_CSR_BEGIN);
    return strip_whitespace(csr_pem.substr(body_start, end - body_start));
}

std::string der_base64_to_pem(const std::string& der_base64) {
    const auto der = evse_security::EvseSecurity::base64_decode_to_bytes(strip_whitespace(der_base64));
    if (der.empty()) {
        return {};
    }
    const unsigned char* data = der.data();
    const X509Ptr cert(d2i_X509(nullptr, &data, static_cast<long>(der.size())));
    if (!cert) {
        return {};
    }
    return x509_to_pem(cert.get());
}

std::vector<std::string> pkcs7_base64_to_pem_certificates(const std::string& body) {
    // Bare PEM certificate(s)
    if (body.find(PEM_CERT_BEGIN) != std::string::npos) {
        std::vector<std::string> out;
        for (auto& cert : pem_to_x509_list(body)) {
            out.push_back(x509_to_pem(cert.get()));
        }
        return out;
    }

    std::string base64 = body;
    // PEM armored PKCS#7
    const auto armored_begin = body.find("-----BEGIN PKCS7-----");
    if (armored_begin != std::string::npos) {
        const auto start = armored_begin + std::strlen("-----BEGIN PKCS7-----");
        const auto end = body.find("-----END PKCS7-----", start);
        base64 = body.substr(start, end == std::string::npos ? std::string::npos : end - start);
    }
    base64 = strip_whitespace(base64);
    if (base64.empty()) {
        return {};
    }

    const auto der = evse_security::EvseSecurity::base64_decode_to_bytes(base64);
    if (der.empty()) {
        return {};
    }
    auto certs = pkcs7_der_to_pem(der);
    if (!certs.empty()) {
        return certs;
    }

    // Defensive: a bare DER certificate
    const unsigned char* data = der.data();
    const X509Ptr single(d2i_X509(nullptr, &data, static_cast<long>(der.size())));
    if (single) {
        certs.push_back(x509_to_pem(single.get()));
    }
    return certs;
}

std::optional<CertificateSummary> describe_certificate(const std::string& certificate_pem) {
    const X509Ptr cert = pem_to_x509(certificate_pem);
    if (!cert) {
        return std::nullopt;
    }
    CertificateSummary summary;
    summary.subject = name_to_string(X509_get_subject_name(cert.get()));
    summary.issuer = name_to_string(X509_get_issuer_name(cert.get()));
    summary.common_name = common_name_of(cert.get());
    summary.not_before = asn1_time_to_string(X509_get0_notBefore(cert.get()));
    summary.not_after = asn1_time_to_string(X509_get0_notAfter(cert.get()));
    summary.public_key_algorithm = public_key_algorithm_of(cert.get());
    summary.self_signed = is_self_signed(cert.get());
    summary.is_ca = X509_check_ca(cert.get()) != 0;

    const ASN1_INTEGER* serial = X509_get0_serialNumber(cert.get());
    if (serial != nullptr) {
        if (BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr)) {
            if (char* hex = BN_bn2hex(bn)) {
                summary.serial_number = hex;
                OPENSSL_free(hex);
            }
            BN_free(bn);
        }
    }
    return summary;
}

std::size_t count_pem_certificates(const std::string& pem) {
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = pem.find(PEM_CERT_BEGIN, pos)) != std::string::npos) {
        ++count;
        pos += std::strlen(PEM_CERT_BEGIN);
    }
    return count;
}

std::optional<std::string> build_leaf_chain(const std::string& csr_pem,
                                            const std::vector<std::string>& enrolled_certificates,
                                            const std::vector<std::string>& ca_certificates, std::string& error,
                                            std::optional<std::string>* chain_top_issuer_pem) {
    error.clear();

    const BioPtr csr_bio(BIO_new_mem_buf(csr_pem.data(), static_cast<int>(csr_pem.size())));
    const X509ReqPtr csr(csr_bio ? PEM_read_bio_X509_REQ(csr_bio.get(), nullptr, nullptr, nullptr) : nullptr);
    if (!csr) {
        error = "the CSR is not a PEM encoded PKCS#10 request";
        return std::nullopt;
    }
    EVP_PKEY* csr_key = X509_REQ_get0_pubkey(csr.get());
    if (csr_key == nullptr) {
        error = "the CSR carries no public key";
        return std::nullopt;
    }

    // Pool of candidates: response certificates first (the leaf is expected there), then the CA chain
    std::vector<X509Ptr> pool;
    for (const auto& pem : enrolled_certificates) {
        for (auto& cert : pem_to_x509_list(pem)) {
            pool.push_back(std::move(cert));
        }
    }
    for (const auto& pem : ca_certificates) {
        for (auto& cert : pem_to_x509_list(pem)) {
            pool.push_back(std::move(cert));
        }
    }
    if (pool.empty()) {
        error = "no certificates were returned by the PKI";
        return std::nullopt;
    }

    X509* leaf = nullptr;
    for (const auto& cert : pool) {
        EVP_PKEY* key = X509_get0_pubkey(cert.get());
        if (key != nullptr && EVP_PKEY_eq(key, csr_key) == 1) {
            leaf = cert.get();
            break;
        }
    }
    if (leaf == nullptr) {
        error = "none of the returned certificates matches the public key of the CSR";
        return std::nullopt;
    }

    std::string chain = x509_to_pem(leaf);
    std::vector<X509*> used{leaf};
    X509* current = leaf;
    std::optional<std::string> top_issuer;

    while (!is_self_signed(current)) {
        X509* issuer = nullptr;
        for (const auto& candidate : pool) {
            if (std::find(used.begin(), used.end(), candidate.get()) != used.end()) {
                continue;
            }
            if (X509_NAME_cmp(X509_get_subject_name(candidate.get()), X509_get_issuer_name(current)) != 0) {
                continue;
            }
            if (X509_verify(current, X509_get0_pubkey(candidate.get())) != 1) {
                continue;
            }
            issuer = candidate.get();
            break;
        }
        if (issuer == nullptr) {
            // The chain ends here; the caller checks whether the issuer is an installed root
            break;
        }
        used.push_back(issuer);
        if (is_self_signed(issuer)) {
            top_issuer = x509_to_pem(issuer);
            break;
        }
        chain += x509_to_pem(issuer);
        current = issuer;
    }

    if (chain_top_issuer_pem != nullptr) {
        *chain_top_issuer_pem = top_issuer;
    }
    if (used.size() == 1 && !is_self_signed(leaf)) {
        error =
            "no issuing sub-CA certificate found for the leaf (issuer: " + name_to_string(X509_get_issuer_name(leaf)) +
            ")";
        return std::nullopt;
    }
    return chain;
}

} // namespace opcp
