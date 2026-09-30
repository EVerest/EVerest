// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/x509_helper.hpp>

#include <algorithm>

#include <openssl/err.h>

#include <iso15118/detail/helper.hpp>

namespace iso15118::x509 {

X509_ptr der_to_x509(const std::vector<uint8_t>& der) {
    return openssl::der_to_certificate(der.data(), der.size());
}

std::string cert_to_pem(X509* cert) {
    return openssl::certificate_to_pem(cert);
}

std::string subject_common_name(X509* cert) {
    const auto subject = openssl::certificate_subject(cert);
    const auto cn = subject.find("CN");
    return cn != subject.end() ? cn->second : std::string{};
}

std::string strip_dashes(std::string in) {
    in.erase(std::remove(in.begin(), in.end(), '-'), in.end());
    return in;
}

PKEY_ptr load_private_key(const std::string& pem, const std::optional<std::string>& password) {
    auto pkey = openssl::pem_to_private_key(pem, password ? password->c_str() : nullptr);
    if (pkey == nullptr) {
        logf_error("PnC: failed to load the private key from PEM");
        ERR_clear_error();
    }
    return pkey;
}

void log_verified_chain(X509_STORE_CTX* ctx) {
    const auto* chain = X509_STORE_CTX_get0_chain(ctx);
    const int depth = sk_X509_num(chain);
    char anchor[256] = {};
    if (depth > 0) {
        X509_NAME_oneline(X509_get_subject_name(sk_X509_value(chain, depth - 1)), anchor, sizeof(anchor));
    }
    logf_info("PnC: contract chain verified locally, %d certificates, trust anchor %s", depth, anchor);
}

} // namespace iso15118::x509
