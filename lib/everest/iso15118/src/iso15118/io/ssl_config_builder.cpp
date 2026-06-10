// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/io/ssl_config_builder.hpp>

#include <utility>
#include <vector>

#include <everest/tls/tls.hpp>

#include <iso15118/config.hpp>

namespace iso15118::io {

std::vector<tls::Server::certificate_config_t> build_chain_configs(const config::SSLConfig& cfg) {
    std::vector<tls::Server::certificate_config_t> chains;
    chains.reserve(cfg.chains.size());

    for (const auto& src : cfg.chains) {
        std::vector<tls::ConfigItem> ocsp_items;
        ocsp_items.reserve(src.ocsp_response_files.size());
        for (const auto& f : src.ocsp_response_files) {
            ocsp_items.emplace_back(f.has_value() ? tls::ConfigItem(f.value()) : tls::ConfigItem(nullptr));
        }

        tls::Server::certificate_config_t chain_cfg{};
        chain_cfg.certificate_chain_file = src.path_certificate_chain;
        chain_cfg.private_key_file = src.path_certificate_key;
        if (src.private_key_password.has_value()) {
            chain_cfg.private_key_password = src.private_key_password.value();
        }
        if (src.trust_anchor_pem.has_value()) {
            chain_cfg.trust_anchor_pem = src.trust_anchor_pem.value();
        }
        chain_cfg.ocsp_response_files = std::move(ocsp_items);
        chains.push_back(std::move(chain_cfg));
    }

    return chains;
}

} // namespace iso15118::io
