// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <everest/tls/openssl_util.hpp>

#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>

// X.509 helpers shared by the ISO 15118-2 and ISO 15118-20 SECC crypto units: thin adapters over libtls for
// the DER/PEM-in-memory shapes the message layer works with. Internal header: OpenSSL types are fine here,
// the public d2/d20 crypto headers keep them out.
namespace iso15118::x509 {

using X509_ptr = openssl::certificate_ptr;
using PKEY_ptr = openssl::pkey_ptr;

X509_ptr der_to_x509(const std::vector<uint8_t>& der);

std::string cert_to_pem(X509* cert);

// Subject CommonName, empty when absent.
std::string subject_common_name(X509* cert);

std::string strip_dashes(std::string in);

// Empty and logged when the PEM does not hold a usable private key.
PKEY_ptr load_private_key(const std::string& pem, const std::optional<std::string>& password);

// Depth and trust anchor of the chain X509_verify_cert just accepted.
void log_verified_chain(X509_STORE_CTX* ctx);

} // namespace iso15118::x509
