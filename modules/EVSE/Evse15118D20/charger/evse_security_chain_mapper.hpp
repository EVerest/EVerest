// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>

#include <generated/types/evse_security.hpp>
#include <iso15118/config.hpp>

namespace module::charger {

/// \brief Maps the multi-chain result of evse_security::get_all_valid_certificates_info into a
/// vector of iso15118::config::ChainConfig — one ChainConfig per accepted CertificateInfo.
///
/// Each accepted CertificateInfo is mapped to one ChainConfig:
///  - path_certificate_chain: chain.certificate when set, else chain.certificate_single
///  - path_certificate_key: chain.key
///  - private_key_password: chain.password
///  - ocsp_response_files: one entry per chain.ocsp[] entry in chain order, forwarding ocsp_path
///    verbatim (std::nullopt is preserved, marking "no OCSP staple for this position")
///  - trust_anchor_pem: chain.certificate_root (the self-signed V2G root PEM that issued this leaf;
///    drives TLS 1.3 / trusted_ca_keys chain selection)
///
/// Returns an empty vector when:
///  - the result status is not Accepted, or
///  - the info vector is empty, or
///  - no usable chain entries remain after filtering (entries lacking both `certificate`
///    and `certificate_single` are skipped).
///
/// Trust-anchor paths and module-level TLS flags are not this function's concern; the caller
/// assembles those separately before forwarding to the controller.
std::vector<iso15118::config::ChainConfig>
map_valid_chains(const types::evse_security::GetCertificateFullInfoResult& result);

/// \brief True when a certificate_store_update event affects the V2G SSL config:
/// a V2G leaf event, or a V2G/MO CA event. False otherwise (incl. an event carrying
/// neither leaf nor CA type). Cheap gate evaluated before any evse_security RPC.
bool is_relevant_certificate_store_update(const types::evse_security::CertificateStoreUpdate& event);

/// \brief What to do with a freshly rebuilt SSL config after a relevant store update.
enum class CertUpdateAction {
    PreserveLastGood, //!< rebuild produced no usable chains; keep the controller's current config
    Apply,            //!< rebuild has at least one chain; push it to the controller
};

/// \brief Decide whether to apply a rebuilt SSL config or preserve the last-good one.
CertUpdateAction decide_certificate_store_update(const iso15118::config::SSLConfig& refreshed);

} // namespace module::charger
