// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <openssl/evp.h>
#include <openssl/x509.h>

// X.509 helpers shared by the ISO 15118-2 and ISO 15118-20 SECC crypto units. Internal header: OpenSSL
// types are fine here, the public d2/d20 crypto headers keep them out.
namespace iso15118::x509 {

using X509_ptr = std::unique_ptr<X509, decltype(&X509_free)>;
using PKEY_ptr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;

X509_ptr der_to_x509(const std::vector<uint8_t>& der);

std::string cert_to_pem(X509* cert);

// Subject CommonName, empty when absent.
std::string subject_common_name(X509* cert);

std::string strip_dashes(std::string in);

std::string der_chain_to_pem(const std::vector<uint8_t>& leaf_der, const std::vector<std::vector<uint8_t>>& subs_der);

std::vector<std::vector<uint8_t>> pem_chain_to_der(const std::string& pem);

} // namespace iso15118::x509
