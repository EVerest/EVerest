// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "evse_security_chain_mapper.hpp"

#include <utility>

#include <everest/logging.hpp>
#include <utils/exceptions.hpp>

namespace module::charger {

std::vector<iso15118::config::ChainConfig>
map_valid_chains(const types::evse_security::GetCertificateFullInfoResult& result) {
    using types::evse_security::GetCertificateInfoStatus;

    if (result.status != GetCertificateInfoStatus::Accepted) {
        EVLOG_warning << "get_all_valid_certificates_info returned non-Accepted status: "
                      << types::evse_security::get_certificate_info_status_to_string(result.status);
        return {};
    }

    if (result.info.empty()) {
        EVLOG_warning << "get_all_valid_certificates_info returned no certificate info";
        return {};
    }

    std::vector<iso15118::config::ChainConfig> chains;
    chains.reserve(result.info.size());

    for (const auto& chain : result.info) {
        std::string path_chain;
        if (chain.certificate.has_value()) {
            path_chain = chain.certificate.value();
        } else if (chain.certificate_single.has_value()) {
            path_chain = chain.certificate_single.value();
        } else {
            EVLOG_warning << "Skipping certificate info entry with neither certificate nor certificate_single set";
            continue;
        }

        std::vector<std::optional<std::string>> ocsp_response_files;
        if (chain.ocsp.has_value()) {
            ocsp_response_files.reserve(chain.ocsp->size());
            for (const auto& entry : *chain.ocsp) {
                // one entry per chain certificate, in chain order; nullopt means
                // "no OCSP staple for this position" and must be preserved so the
                // TLS layer can pair staples with certificates positionally
                ocsp_response_files.push_back(entry.ocsp_path);
            }
        }

        if (not chain.certificate_root.has_value()) {
            EVLOG_warning << "Certificate chain (key: " << chain.key
                          << ") has no certificate_root; it will be excluded from TLS 1.3 certificate_authorities / "
                             "trusted_ca_keys selection";
        }

        iso15118::config::ChainConfig chain_config{};
        chain_config.path_certificate_chain = std::move(path_chain);
        chain_config.path_certificate_key = chain.key;
        chain_config.private_key_password = chain.password;
        chain_config.ocsp_response_files = std::move(ocsp_response_files);
        chain_config.trust_anchor_pem = chain.certificate_root; // inline PEM of the chain's V2G root
        chains.push_back(std::move(chain_config));
    }

    if (chains.empty()) {
        EVLOG_warning << "get_all_valid_certificates_info produced no usable chain entries";
        return {};
    }

    return chains;
}

bool is_relevant_certificate_store_update(const types::evse_security::CertificateStoreUpdate& event) {
    const bool relevant_leaf = event.leaf_certificate_type.has_value() &&
                               event.leaf_certificate_type.value() == types::evse_security::LeafCertificateType::V2G;
    const bool relevant_ca = event.ca_certificate_type.has_value() &&
                             (event.ca_certificate_type.value() == types::evse_security::CaCertificateType::V2G ||
                              event.ca_certificate_type.value() == types::evse_security::CaCertificateType::MO);
    return relevant_leaf || relevant_ca;
}

CertUpdateAction decide_certificate_store_update(const iso15118::config::SSLConfig& refreshed) {
    return refreshed.chains.empty() ? CertUpdateAction::PreserveLastGood : CertUpdateAction::Apply;
}

void resync_ssl_config(const std::function<iso15118::config::SSLConfig()>& rebuild,
                       const std::function<void(iso15118::config::SSLConfig)>& apply) {
    try {
        auto refreshed = rebuild();
        switch (decide_certificate_store_update(refreshed)) {
        case CertUpdateAction::PreserveLastGood:
            EVLOG_error << "SSL config refresh produced no usable V2G certificate chains; "
                           "preserving last-good SSL config";
            return;
        case CertUpdateAction::Apply:
            EVLOG_info << "SSL config refresh: applying SSL config with " << refreshed.chains.size() << " chain(s)";
            apply(std::move(refreshed));
            return;
        }
    } catch (const Everest::CmdTimeout& e) {
        EVLOG_error << "SSL config refresh: evse_security RPC timed out; keeping last-good SSL config. "
                       "New V2G certificates will NOT be served until the next refresh: "
                    << e.what();
    } catch (const std::exception& e) {
        EVLOG_error << "SSL config refresh failed; keeping last-good SSL config: " << e.what();
    }
}

void handle_certificate_store_update(const types::evse_security::CertificateStoreUpdate& event,
                                     const std::function<iso15118::config::SSLConfig()>& rebuild,
                                     const std::function<void(iso15118::config::SSLConfig)>& apply) {
    if (not is_relevant_certificate_store_update(event)) {
        return;
    }
    resync_ssl_config(rebuild, apply);
}

StartupChainPolicy decide_startup_empty_chains(iso15118::config::TlsNegotiationStrategy strategy) {
    return strategy == iso15118::config::TlsNegotiationStrategy::ENFORCE_TLS ? StartupChainPolicy::Throw
                                                                             : StartupChainPolicy::WarnAndContinue;
}

} // namespace module::charger
