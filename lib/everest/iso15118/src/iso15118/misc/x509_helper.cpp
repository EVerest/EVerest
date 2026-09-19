// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/x509_helper.hpp>

#include <algorithm>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>

namespace iso15118::x509 {

X509_ptr der_to_x509(const std::vector<uint8_t>& der) {
    const unsigned char* p = der.data();
    return X509_ptr(d2i_X509(nullptr, &p, static_cast<long>(der.size())), &X509_free);
}

std::string cert_to_pem(X509* cert) {
    BIO* bio = BIO_new(BIO_s_mem());
    if (bio == nullptr) {
        return {};
    }
    std::string pem;
    if (PEM_write_bio_X509(bio, cert) == 1) {
        char* data = nullptr;
        const long n = BIO_get_mem_data(bio, &data);
        if (n > 0 and data != nullptr) {
            pem.assign(data, static_cast<std::size_t>(n));
        }
    }
    BIO_free(bio);
    return pem;
}

std::string subject_common_name(X509* cert) {
    std::string cn;
    X509_NAME* name = X509_get_subject_name(cert);
    if (name == nullptr) {
        return cn;
    }
    char buf[256] = {0};
    const int len = X509_NAME_get_text_by_NID(name, NID_commonName, buf, sizeof(buf) - 1);
    if (len > 0) {
        cn.assign(buf, static_cast<std::size_t>(len));
    }
    return cn;
}

std::string strip_dashes(std::string in) {
    in.erase(std::remove(in.begin(), in.end(), '-'), in.end());
    return in;
}

std::string der_chain_to_pem(const std::vector<uint8_t>& leaf_der, const std::vector<std::vector<uint8_t>>& subs_der) {
    std::string pem;
    auto append = [&pem](const std::vector<uint8_t>& der) {
        auto x = der_to_x509(der);
        if (x != nullptr) {
            pem += cert_to_pem(x.get());
        }
    };
    append(leaf_der);
    for (const auto& sub : subs_der) {
        append(sub);
    }
    return pem;
}

std::vector<std::vector<uint8_t>> pem_chain_to_der(const std::string& pem) {
    std::vector<std::vector<uint8_t>> out;
    BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
    if (bio == nullptr) {
        return out;
    }
    X509* cert = nullptr;
    while ((cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr)) != nullptr) {
        unsigned char* der = nullptr;
        const int len = i2d_X509(cert, &der);
        if (len > 0 and der != nullptr) {
            out.emplace_back(der, der + len);
        }
        OPENSSL_free(der);
        X509_free(cert);
    }
    BIO_free(bio);
    ERR_clear_error(); // the loop terminates on a benign "no start line" PEM error
    return out;
}

} // namespace iso15118::x509
