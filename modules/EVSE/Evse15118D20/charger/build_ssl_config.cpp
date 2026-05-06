// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "build_ssl_config.hpp"

#include <utility>

#include <everest/logging.hpp>

namespace module::charger {

std::optional<iso15118::config::SSLConfig>
build_ssl_config(const types::evse_security::GetCertificateFullInfoResult& result,
                 const std::string& path_certificate_v2g_root, const std::string& path_certificate_mo_root) {
    using types::evse_security::GetCertificateInfoStatus;

    if (result.status != GetCertificateInfoStatus::Accepted) {
        EVLOG_warning << "get_all_valid_certificates_info returned non-Accepted status: "
                      << types::evse_security::get_certificate_info_status_to_string(result.status);
        return std::nullopt;
    }

    if (result.info.empty()) {
        EVLOG_warning << "get_all_valid_certificates_info returned no certificate info";
        return std::nullopt;
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
            std::move(path_chain),
            chain.key,
            chain.password,
            std::move(ocsp_response_files),
        });
    }

    if (chains.empty()) {
        EVLOG_warning << "get_all_valid_certificates_info produced no usable chain entries";
        return std::nullopt;
    }

    iso15118::config::SSLConfig cfg{};
    cfg.backend = iso15118::config::CertificateBackend::EVEREST_LAYOUT;
    cfg.chains = std::move(chains);
    cfg.path_certificate_v2g_root = path_certificate_v2g_root;
    cfg.path_certificate_mo_root = path_certificate_mo_root;
    return cfg;
}

} // namespace module::charger
