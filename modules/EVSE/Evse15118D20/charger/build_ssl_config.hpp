// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>

#include <generated/types/evse_security.hpp>
#include <iso15118/config.hpp>

namespace module::charger {

/// \brief Maps the multi-chain result of evse_security::get_all_valid_certificates_info into an
/// iso15118::config::SSLConfig snapshot suitable for TbdController::set_ssl_config.
///
/// Returns std::nullopt when:
///  - the result status is not Accepted, or
///  - the info vector is empty, or
///  - no usable chain entries remain after filtering (entries lacking both `certificate`
///    and `certificate_single` are skipped).
///
/// On success, each accepted CertificateInfo is mapped to one ChainConfig:
///  - path_certificate_chain: chain.certificate.value_or(chain.certificate_single.value_or(""))
///  - path_certificate_key: chain.key
///  - private_key_password: chain.password
///  - ocsp_response_files: collected from chain.ocsp[].ocsp_path (entries without ocsp_path are skipped)
///
/// Trust-anchor paths are forwarded from the parameters; the caller resolves them via
/// evse_security::get_verify_file. Module-level TLS flags (enforce_tls_1_3, logging,
/// key-logging path) are left at their SSLConfig defaults; the integration site overlays
/// them from the module config before forwarding to the controller.
std::optional<iso15118::config::SSLConfig>
build_ssl_config(const types::evse_security::GetCertificateFullInfoResult& result,
                 const std::string& path_certificate_v2g_root, const std::string& path_certificate_mo_root);

} // namespace module::charger
