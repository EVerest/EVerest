// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "evse_security_chain_mapper.hpp"

#include <utility>

#include <everest/logging.hpp>

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

        std::vector<std::string> ocsp_response_files;
        if (chain.ocsp.has_value()) {
            ocsp_response_files.reserve(chain.ocsp->size());
            for (const auto& entry : *chain.ocsp) {
                if (entry.ocsp_path.has_value()) {
                    ocsp_response_files.push_back(*entry.ocsp_path);
                }
            }
        }

        chains.push_back(iso15118::config::ChainConfig{
            std::move(path_chain), chain.key, chain.password, std::move(ocsp_response_files),
            chain.certificate_root, // trust_anchor_pem (inline PEM of the chain's V2G root)
        });
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

} // namespace module::charger
