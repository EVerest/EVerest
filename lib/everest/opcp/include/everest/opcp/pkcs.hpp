// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>
#include <vector>

/// \file pkcs.hpp
/// Encoding helpers around the EST wire formats: PKCS#10 requests are sent as base64 DER, certificates come
/// back as base64 encoded PKCS#7 "certs-only" structures, RCP roots as base64 DER.
namespace opcp {

/// Strips the PEM armor of a `CERTIFICATE REQUEST` and returns its base64 body without line breaks (which is
/// exactly the base64 DER the EST endpoints expect). Empty when the input is not a PEM CSR.
std::string pem_csr_to_base64_der(const std::string& csr_pem);

/// Removes all whitespace (EST responses are line wrapped base64)
std::string strip_whitespace(const std::string& input);

/// Converts a base64 DER certificate (RCP `caCertificate`) into PEM. Empty on decode failure.
std::string der_base64_to_pem(const std::string& der_base64);

/// Decodes a base64 PKCS#7 certs-only blob into the contained certificates as PEM strings. Accepts a
/// PKCS#7 in PEM armor or a bare PEM certificate (chain) as well. Empty vector on failure.
std::vector<std::string> pkcs7_base64_to_pem_certificates(const std::string& body);

/// Human readable facts about a certificate, for logs and CLI summaries
struct CertificateSummary {
    std::string subject;
    std::string issuer;
    std::string common_name;
    std::string serial_number;
    std::string not_before; ///< RFC 3339 UTC
    std::string not_after;  ///< RFC 3339 UTC
    std::string public_key_algorithm;
    bool self_signed{false};
    bool is_ca{false};
};
std::optional<CertificateSummary> describe_certificate(const std::string& certificate_pem);

/// Builds the certificate chain that is stored for a SECC leaf: leaf first, followed by the issuing sub-CAs
/// in order (sub-CA 2, sub-CA 1). The leaf is the certificate whose public key matches \p csr_pem; sub-CAs
/// are looked up in both \p enrolled_certificates (the simpleenroll response) and \p ca_certificates (the
/// cacerts response). Self-signed roots are never part of the chain - they live in the CA bundle.
/// \param error human readable reason when std::nullopt is returned (no leaf for the key, broken chain)
/// \param chain_top_issuer_pem set to the PEM of the certificate the chain walk stopped at (a root from
///        the inputs, if any) so the caller can check it against the installed V2G roots
std::optional<std::string> build_leaf_chain(const std::string& csr_pem,
                                            const std::vector<std::string>& enrolled_certificates,
                                            const std::vector<std::string>& ca_certificates, std::string& error,
                                            std::optional<std::string>* chain_top_issuer_pem = nullptr);

/// Number of certificates in a PEM string
std::size_t count_pem_certificates(const std::string& pem);

} // namespace opcp
