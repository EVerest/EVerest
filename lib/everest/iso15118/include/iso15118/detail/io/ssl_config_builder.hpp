// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <vector>

#include <everest/tls/tls.hpp>

#include <iso15118/config.hpp>

namespace iso15118::io {

/// \brief Map iso15118 SSLConfig chains to tls::Server certificate chain configs.
/// Each ChainConfig.trust_anchor_pem (the chain's self-signed root PEM) is forwarded
/// to certificate_config_t.trust_anchor_pem so tls::Server can admit the chain into its
/// TLS 1.3 / trusted_ca_keys selection pool. Exposed (rather than file-local) for unit testing.
std::vector<tls::Server::certificate_config_t> build_chain_configs(const config::SSLConfig& cfg);

} // namespace iso15118::io
