// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "OpcpCertificateManager.hpp"

#include <everest/logging.hpp>

namespace module {

void OpcpCertificateManager::init() {
    invoke_init(*p_main);

    std::string error;
    auto manager_config = manager_config_from(
        config.environment, config.api_base_url, config.est_base_url, config.simplereenroll_path, config.cacerts_path,
        config.ca, config.server_ca_bundle, config.manage_iso15118_2, config.manage_iso15118_20,
        config.common_name_iso2, config.common_name_iso20, config.organization, config.country, config.use_tpm,
        config.renewal_threshold_days, config.check_interval_s, config.initial_delay_s, config.retry_backoff_s,
        config.sync_root_certificates, config.root_types, config.root_sync_interval_s, config.http_timeout_ms, error);
    if (!manager_config.has_value()) {
        EVLOG_error << "OpcpCertificateManager is misconfigured and stays inactive: " << error;
        return;
    }
    if (!config.manage_iso15118_2 && !config.manage_iso15118_20 && !config.sync_root_certificates) {
        EVLOG_warning << "OpcpCertificateManager has nothing to do: no leaf is managed and root synchronisation is off";
    }

    security_store = std::make_unique<InterfaceSecurityStore>(*r_security);
    http_client = std::make_unique<opcp::CurlHttpClient>(manager_config->http);
    certificate_manager = std::make_unique<CertificateManager>(
        std::move(*manager_config), *security_store, *http_client,
        [this](const types::opcp_certificate_manager::Status& status) { p_main->publish_status(status); });

    EVLOG_info << "OpcpCertificateManager: environment " << config.environment << ", EST "
               << certificate_manager->current_status().leafs.size() << " leaf profile(s) tracked, ISO 15118-2 "
               << (config.manage_iso15118_2 ? "managed" : "not managed") << ", ISO 15118-20 "
               << (config.manage_iso15118_20 ? "managed" : "not managed") << ", root sync "
               << (config.sync_root_certificates ? "on" : "off");
}

void OpcpCertificateManager::ready() {
    invoke_ready(*p_main);
    if (certificate_manager) {
        certificate_manager->start();
        p_main->publish_status(certificate_manager->current_status());
    }
}

void OpcpCertificateManager::shutdown() {
    if (certificate_manager) {
        certificate_manager->stop();
    }
    invoke_shutdown(*p_main);
}

} // namespace module
