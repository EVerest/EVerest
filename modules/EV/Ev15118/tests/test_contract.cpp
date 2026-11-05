// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "test_contract.hpp"

#include <memory>
#include <stdexcept>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

namespace test_contract {

namespace {

using BIO_ptr = std::unique_ptr<BIO, decltype(&BIO_free)>;

void require(bool ok, const char* what) {
    if (not ok) {
        throw std::runtime_error(std::string("test contract: ") + what);
    }
}

template <typename Write> std::string to_pem(Write write) {
    BIO_ptr bio{BIO_new(BIO_s_mem()), &BIO_free};
    require(bio != nullptr and write(bio.get()) == 1, "PEM encoding failed");
    char* data = nullptr;
    const long size = BIO_get_mem_data(bio.get(), &data);
    return {data, static_cast<std::size_t>(size)};
}

} // namespace

Contract generate(const std::string& common_name) {
    std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)> key{EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "prime256v1"),
                                                            &EVP_PKEY_free};
    require(key != nullptr, "key generation failed");

    std::unique_ptr<X509, decltype(&X509_free)> cert{X509_new(), &X509_free};
    require(cert != nullptr and X509_set_version(cert.get(), 2) == 1, "certificate allocation failed");
    require(ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1) == 1, "setting the serial number failed");
    require(X509_gmtime_adj(X509_getm_notBefore(cert.get()), -3600) != nullptr and
                X509_gmtime_adj(X509_getm_notAfter(cert.get()), 365L * 24 * 3600) != nullptr,
            "setting the validity failed");
    require(X509_set_pubkey(cert.get(), key.get()) == 1, "setting the public key failed");
    X509_NAME* name = X509_get_subject_name(cert.get());
    require(X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                       reinterpret_cast<const unsigned char*>(common_name.c_str()), -1, -1, 0) == 1,
            "setting the CommonName failed");
    require(X509_set_issuer_name(cert.get(), name) == 1, "setting the issuer failed");
    require(X509_sign(cert.get(), key.get(), EVP_sha256()) > 0, "signing failed");

    return {to_pem([&](BIO* bio) { return PEM_write_bio_X509(bio, cert.get()); }), to_pem([&](BIO* bio) {
                return PEM_write_bio_PrivateKey_traditional(bio, key.get(), nullptr, nullptr, 0, nullptr, nullptr);
            })};
}

} // namespace test_contract
