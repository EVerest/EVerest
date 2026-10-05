// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <functional>
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

/// \brief Rebuild the SSLConfig via \p rebuild and either forward it via \p apply or preserve
/// the last-good config when the rebuild yields no chains. All exceptions thrown by \p rebuild
/// (incl. Everest::CmdTimeout from the evse_security RPC) are caught and logged; nothing escapes,
/// so the caller's thread survives RPC failures. Applying a config identical to the current one is
/// harmless (apply is a single atomic write), so this is safe to invoke as a one-shot re-sync to
/// close the startup window between the initial config build and store-update subscription.
void resync_ssl_config(const std::function<iso15118::config::SSLConfig()>& rebuild,
                       const std::function<void(iso15118::config::SSLConfig)>& apply);

/// \brief Handles a certificate_store_update event: gates on
/// is_relevant_certificate_store_update(), then delegates to resync_ssl_config().
void handle_certificate_store_update(const types::evse_security::CertificateStoreUpdate& event,
                                     const std::function<iso15118::config::SSLConfig()>& rebuild,
                                     const std::function<void(iso15118::config::SSLConfig)>& apply);

/// \brief What ready() must do when the initial SSL config carries no usable chains.
enum class StartupChainPolicy {
    Throw,           //!< refuse to start: TLS is mandatory but cannot be served
    WarnAndContinue, //!< start anyway; TLS connection attempts fail until certificates arrive
};

/// \brief Startup policy for an empty chain list: ENFORCE_TLS demands a usable chain (Throw);
/// every other negotiation strategy can operate without TLS (WarnAndContinue).
StartupChainPolicy decide_startup_empty_chains(iso15118::config::TlsNegotiationStrategy strategy);

} // namespace module::charger
