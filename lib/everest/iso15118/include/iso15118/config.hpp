// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace iso15118::config {

enum class TlsNegotiationStrategy {
    ACCEPT_CLIENT_OFFER,
    ENFORCE_TLS,
    ENFORCE_NO_TLS,
};

enum class CertificateBackend {
    EVEREST_LAYOUT,
    JOSEPPA_LAYOUT,
};

/**
 * \brief one server certificate chain plus the matching key and OCSP responses
 *
 * Multiple chains may be configured to support TLS 1.3 multi-chain selection
 * driven by the peer's certificate_authorities extension (RFC 8446 §4.2.4).
 */
struct ChainConfig {
    std::string path_certificate_chain;                //!< PEM file: leaf followed by intermediate CAs
    std::string path_certificate_key;                  //!< PEM file: private key for the leaf
    std::optional<std::string> private_key_password{}; //!< optional password for the private key
    std::vector<std::string> ocsp_response_files{};    //!< OCSP DER files in chain order
    //!< inline PEM of the chain's self-signed root; drives TLS 1.3 chain selection
    //!< (certificate_authorities) and TLS 1.2 trusted_ca_keys. Empty disables this chain
    //!< from selection (it can still be the default chain when first in the list).
    std::optional<std::string> trust_anchor_pem{};
};

/**
 * \brief SSL configuration for the ISO 15118 SECC TLS server
 *
 * Holds one or more server certificate chains, the trust anchors used to
 * verify peer (vehicle) certificates, and key/OCSP logging knobs. A snapshot
 * of this struct is consumed by the TLS adapter on init or on a
 * certificate_store_update event.
 */
struct SSLConfig {
    CertificateBackend backend{CertificateBackend::EVEREST_LAYOUT};
    //!< Used by the JOSEPPA_LAYOUT
    std::string config_string;
    //!< Used by the EVEREST_LAYOUT: one or more server certificate chains
    std::vector<ChainConfig> chains;
    std::string path_certificate_v2g_root;        //!< V2G root trust anchor (PEM)
    std::string path_certificate_mo_root;         //!< MO root trust anchor (PEM)
    bool enable_ssl_logging{false};               //!< verbose SSL logging
    bool enable_tls_key_logging{false};           //!< write SSLKEYLOGFILE entries
    bool enforce_tls_1_3{false};                  //!< require TLS 1.3 minimum
    std::filesystem::path tls_key_logging_path{}; //!< destination directory for keylog
};

} // namespace iso15118::config
