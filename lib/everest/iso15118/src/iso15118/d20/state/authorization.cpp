// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2026 Pionix GmbH and Contributors to EVerest
#include <algorithm>

#include <iso15118/d20/state/authorization.hpp>
#include <iso15118/d20/state/service_discovery.hpp>
#include <iso15118/d20/timeout.hpp>

#include <iso15118/detail/base64.hpp>
#include <iso15118/detail/d20/context_helper.hpp>
#include <iso15118/detail/d20/crypto.hpp>
#include <iso15118/detail/d20/state/authorization.hpp>
#include <iso15118/detail/d20/state/session_stop.hpp>

#include <iso15118/detail/helper.hpp>

namespace iso15118::d20::state {

namespace dt = message_20::datatypes;

using AuthStatus = dt::AuthStatus;

static bool find_auth_service_in_offered_services(const dt::Authorization& req_selected_auth_service,
                                                  const d20::Session& session) {
    auto& offered_auth_services = session.offered_services.auth_services;
    return std::find(offered_auth_services.begin(), offered_auth_services.end(), req_selected_auth_service) !=
           offered_auth_services.end();
}

dt::ResponseCode pnc_rejection_code(const AuthorizationResponse& response) {
    switch (response.get_certificate_status()) {
    case CertificateStatus::CertificateExpired:
        return dt::ResponseCode::WARNING_CertificateExpired;
    case CertificateStatus::CertificateRevoked:
        return dt::ResponseCode::WARNING_CertificateRevoked;
    case CertificateStatus::CertChainError:
    case CertificateStatus::SignatureError:
    case CertificateStatus::NoCertificateAvailable:
        return dt::ResponseCode::WARNING_CertificateValidationError;
    case CertificateStatus::ContractCancelled: // no -20 code of its own
    case CertificateStatus::Accepted:
    default:
        return response.is_token_unknown() ? dt::ResponseCode::WARNING_eMSPUnknown
                                           : dt::ResponseCode::WARNING_GeneralPnCAuthorizationError;
    }
}

message_20::AuthorizationResponse handle_request(const message_20::AuthorizationRequest& req,
                                                 const d20::Session& session,
                                                 const dt::AuthStatus& authorization_status, bool timeout_reached,
                                                 const std::optional<PncOutcome>& pnc) {

    message_20::AuthorizationResponse res = message_20::AuthorizationResponse();

    if (validate_and_setup_header(res.header, session, req.header.session_id) == false) {
        return response_with_code(res, dt::ResponseCode::FAILED_UnknownSession);
    }

    if (timeout_reached) {
        return response_with_code(res, dt::ResponseCode::FAILED);
    }

    // [V2G20-2209] Check if authorization service was offered in authorization_setup res
    if (not find_auth_service_in_offered_services(req.selected_authorization_service, session)) {
        return response_with_code(
            res, dt::ResponseCode::WARNING_AuthorizationSelectionInvalid); // [V2G20-2226] Handling if warning
    }

    auto response_code = dt::ResponseCode::OK;

    switch (req.selected_authorization_service) {
    case dt::Authorization::EIM:
        switch (authorization_status) {
        case AuthStatus::Accepted:
            res.evse_processing = dt::Processing::Finished;
            response_code = dt::ResponseCode::OK;
            break;
        case AuthStatus::Rejected: // Failure [V2G20-2230]
            res.evse_processing = dt::Processing::Finished;
            response_code = dt::ResponseCode::WARNING_EIMAuthorizationFailure;
            break;
        case AuthStatus::Pending:
        default:
            res.evse_processing = dt::Processing::Ongoing;
            response_code = dt::ResponseCode::OK;
            break;
        }
        break;

    case dt::Authorization::PnC:
        if (pnc.has_value()) {
            res.evse_processing = pnc->processing;
            response_code = pnc->code;
        } else {
            res.evse_processing = dt::Processing::Finished;
            response_code = dt::ResponseCode::WARNING_GeneralPnCAuthorizationError;
        }
        break;

    default:
        res.evse_processing = dt::Processing::Finished;
        response_code = dt::ResponseCode::WARNING_AuthorizationSelectionInvalid;
        break;
    }

    return response_with_code(res, response_code);
}

void Authorization::enter() {
    logf_debug("Enter state: Authorization");
}

Result Authorization::feed(Event ev) {
    if (m_ctx.session_stopped) {
        return {};
    }

    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto control_data = m_ctx.get_control_event<AuthorizationResponse>()) {
            if (backend_response or (not pnc_attempt and not m_ctx.session.authorization.eim_requested)) {
                return {};
            }
            backend_response = *control_data;
            authorization_status = *control_data ? AuthStatus::Accepted : AuthStatus::Rejected;
        } else if (const auto certificate = m_ctx.get_control_event<CertificateResponse>()) {
            if (not cert_install_forwarded or cert_install_failed or cert_install_raw_response) {
                logf_warning("Authorization: certificate response without a forwarded request; ignored");
                return {};
            }
            if (not certificate->status_accepted or certificate->exi_response_base64.empty()) {
                logf_warning("Authorization: the backend could not provide a contract certificate");
                cert_install_failed = true;
                return {};
            }
            auto raw = base64_decode(certificate->exi_response_base64);
            if (raw.empty()) {
                logf_warning("Authorization: failed to base64-decode the backend certificate response");
                cert_install_failed = true;
                return {};
            }
            cert_install_raw_response = std::move(raw);
        }
        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::ONGOING) {
            if (cert_install_forwarded and not cert_install_raw_response.has_value()) {
                // [V2G20-2225]: the backend did not deliver in time.
                cert_install_failed = true;
            } else if (pnc_attempt or eim_timer_started) {
                timeout_ongoing_reached = true;
            }
        }
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    if (m_ctx.session.authorization.authorized and not cert_install_forwarded and
        m_ctx.peek_request_type() == message_20::Type::ServiceDiscoveryReq) {
        ServiceDiscovery discovery(m_ctx);
        return discovery.feed(ev);
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<message_20::AuthorizationRequest>()) {
        return handle_authorization_request(*req, *variant);
    } else if (const auto req = variant->get_if<message_20::CertificateInstallationRequest>()) {
        return handle_certificate_installation_request(*req, *variant);
    } else if (const auto req = variant->get_if<message_20::SessionStopRequest>()) {
        const auto res = handle_request(*req, m_ctx.session);
        m_ctx.respond(res);
        mark_session_stop_response(m_ctx, *req, res);

        m_ctx.session_stopped = true;
        return {};
    } else {
        logf_warning("Expected AuthorizationReq! But code type id: %d", variant->get_type());

        // Sequence Error
        const message_20::Type req_type = variant->get_type();
        send_sequence_error(req_type, m_ctx);

        m_ctx.session_stopped = true;
        return {};
    }
}

Result Authorization::finish_authorized(dt::Authorization via, bool allow_installation) {
    auto& authorization = m_ctx.session.authorization;
    authorization.authorized = true;
    authorization.authorized_via = via;
    if (via == dt::Authorization::PnC and pnc_attempt.has_value()) {
        authorization.contract_leaf_der = pnc_attempt->leaf_der;
        authorization.emaid = pnc_attempt->emaid;
        authorization.contract_chain_pem = pnc_attempt->chain_pem;
    }
    authorization_status = AuthStatus::Pending; // reset
    pnc_attempt.reset();
    m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
    if (allow_installation and m_ctx.session.offered_services.cert_install_service) {
        return {};
    }
    return m_ctx.create_state<ServiceDiscovery>();
}

Result Authorization::handle_authorization_request(const message_20::AuthorizationRequest& req,
                                                   const message_20::Variant& variant) {
    const bool offered = find_auth_service_in_offered_services(req.selected_authorization_service, m_ctx.session);
    const bool eim = req.selected_authorization_service == dt::Authorization::EIM;

    message_20::Header header;
    if (not validate_and_setup_header(header, m_ctx.session, req.header.session_id)) {
        const auto res = handle_request(req, m_ctx.session, authorization_status, false, std::nullopt);
        m_ctx.respond(res);
        m_ctx.session_stopped = true;
        return {};
    }
    if (cert_install_forwarded or (pnc_attempt and eim) or (eim_timer_started and not eim) or
        (m_ctx.session.authorization.authorized and eim)) {
        send_sequence_error(message_20::Type::AuthorizationReq, m_ctx);
        m_ctx.session_stopped = true;
        return {};
    }
    const auto* signed_mode = std::get_if<dt::PnC_ASReqAuthorizationMode>(&req.authorization_mode);
    if (not eim and offered and signed_mode and
        crypto::verify_signature(variant.get_exi_payload(), signed_mode->contract_certificate_chain.certificate,
                                 crypto::SignedElement::PnC_AReqAuthorizationMode) != crypto::SignatureVerdict::Ok) {
        message_20::AuthorizationResponse res;
        res.header = header;
        res.evse_processing = dt::Processing::Finished;
        m_ctx.respond(response_with_code(res, dt::ResponseCode::FAILED_SignatureError));
        m_ctx.session_stopped = true;
        return {};
    }
    const auto poll =
        eim ? std::vector<uint8_t>{} : crypto::authorization_request_without_timestamp(variant.get_exi_payload());
    if (pnc_attempt and (poll.empty() or poll != pnc_attempt->request)) {
        send_sequence_error(message_20::Type::AuthorizationReq, m_ctx);
        m_ctx.session_stopped = true;
        return {};
    }

    if (eim and offered and not timeout_ongoing_reached) {
        if (not m_ctx.session.authorization.eim_requested) {
            m_ctx.feedback.signal(session::feedback::Signal::REQUIRE_AUTH_EIM);
            m_ctx.session.authorization.eim_requested = true;
        }
        if (not eim_timer_started) {
            m_ctx.restart_timeout(d20::TimeoutType::ONGOING, TIMEOUT_EIM_ONGOING);
            eim_timer_started = true;
        }
    }

    std::optional<PncOutcome> pnc_outcome;
    const auto* pnc = std::get_if<dt::PnC_ASReqAuthorizationMode>(&req.authorization_mode);

    if (not eim and offered and not timeout_ongoing_reached) {
        auto& authorization = m_ctx.session.authorization;
        const auto finished = [](dt::ResponseCode code) { return PncOutcome{code, dt::Processing::Finished}; };

        if (pnc == nullptr) {
            pnc_outcome = finished(dt::ResponseCode::WARNING_AuthorizationSelectionInvalid);
        } else if (not gen_challenge.has_value() or pnc->gen_challenge != *gen_challenge) {
            // [V2G20-2565], [V2G20-2216]
            logf_warning("Authorization: GenChallenge does not match the one sent in AuthorizationSetupRes");
            pnc_outcome = finished(dt::ResponseCode::WARNING_ChallengeInvalid);
        } else if (authorization.authorized and
                   pnc->contract_certificate_chain.certificate != authorization.contract_leaf_der) {
            // [V2G20-2702]: the contract of an authorized session is fixed.
            pnc_outcome = finished(dt::ResponseCode::WARNING_GeneralPnCAuthorizationError);
        } else if (authorization.authorized) {
            pnc_outcome = finished(dt::ResponseCode::OK);
        } else if (pnc_attempt.has_value() and pnc_attempt->leaf_der == pnc->contract_certificate_chain.certificate) {
            // Unaltered repetition while the backend answer is pending ([V2G20-1582]).
            switch (authorization_status) {
            case AuthStatus::Accepted:
                pnc_outcome = finished(pnc_attempt->expires_soon ? dt::ResponseCode::OK_CertificateExpiresSoon
                                                                 : dt::ResponseCode::OK);
                break;
            case AuthStatus::Rejected:
                pnc_outcome = finished(backend_response ? pnc_rejection_code(*backend_response)
                                                        : dt::ResponseCode::WARNING_GeneralPnCAuthorizationError);
                break;
            case AuthStatus::Pending:
            default:
                pnc_outcome = PncOutcome{dt::ResponseCode::OK, dt::Processing::Ongoing};
                break;
            }
        } else {
            std::vector<std::vector<uint8_t>> subs(pnc->contract_certificate_chain.sub_certificates.begin(),
                                                   pnc->contract_certificate_chain.sub_certificates.end());
            const auto validation = crypto::validate_contract_chain(pnc->contract_certificate_chain.certificate, subs,
                                                                    m_ctx.session_config.contract_mo_root_path,
                                                                    m_ctx.session_config.contract_v2g_root_path);
            const bool forward = validation.response_code == dt::ResponseCode::OK or
                                 (validation.forwardable and m_ctx.session_config.central_contract_validation_allowed);
            if (not forward) {
                pnc_outcome = finished(validation.response_code);
            } else {
                pnc_attempt = PncAttempt{pnc->contract_certificate_chain.certificate, validation.emaid,
                                         validation.chain_pem, validation.expires_within_14_days, poll};
                authorization_status = AuthStatus::Pending;
                backend_response.reset();
                m_ctx.feedback.require_auth_pnc(validation.emaid, validation.chain_pem);
                // [V2G20-2102]: bounded wait for the backend; 0 keeps waiting.
                const auto timeout_ms = session::auth_timeout_to_ms(m_ctx.session_config.auth_timeout_pnc_s);
                if (timeout_ms > 0) {
                    m_ctx.restart_timeout(d20::TimeoutType::ONGOING, timeout_ms);
                } else {
                    m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
                }
                eim_timer_started = false;
                pnc_outcome = PncOutcome{dt::ResponseCode::OK, dt::Processing::Ongoing};
            }
        }
    }

    const auto res = handle_request(req, m_ctx.session, authorization_status, timeout_ongoing_reached, pnc_outcome);
    m_ctx.respond(res);

    if (res.response_code >= dt::ResponseCode::FAILED) {
        m_ctx.session_stopped = true;
        return {};
    }

    if (res.evse_processing == dt::Processing::Finished) {
        m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
        timeout_ongoing_reached = false;
        eim_timer_started = false;
        if (res.response_code == dt::ResponseCode::OK or
            res.response_code == dt::ResponseCode::OK_CertificateExpiresSoon) {
            return finish_authorized(req.selected_authorization_service,
                                     res.response_code == dt::ResponseCode::OK_CertificateExpiresSoon);
        }
        // A WARNING ends this attempt; the EV may try another chain, switch the service or install a
        // certificate ([V2G20-1978], [V2G20-1583]).
        pnc_attempt.reset();
        authorization_status = AuthStatus::Pending;
        backend_response.reset();
        if (eim) {
            m_ctx.session.authorization.eim_requested = false;
        }
    }
    return {};
}

void Authorization::respond_certificate_installation(const message_20::CertificateInstallationRequest& req,
                                                     dt::ResponseCode code, dt::Processing processing) {
    // [V2G20-2202]: mandatory elements with minimal placeholder content.
    message_20::CertificateInstallationResponse res;
    validate_and_setup_header(res.header, m_ctx.session, req.header.session_id);
    res.evse_processing = processing;
    res.signed_installation_data.id = "id1";
    res.signed_installation_data.contract_certificate_chain.sub_certificates.emplace_back();
    res.remaining_contract_certificate_chains = 0;
    m_ctx.respond(response_with_code(res, code));
}

Result Authorization::handle_certificate_installation_request(const message_20::CertificateInstallationRequest& req,
                                                              const message_20::Variant& variant) {
    if (pnc_attempt or eim_timer_started or not m_ctx.session.offered_services.cert_install_service or
        (cert_install_remaining.has_value() and *cert_install_remaining == 0)) {
        // Not offered, or the last chain was delivered: only AuthorizationReq is allowed ([V2G20-1975]).
        send_sequence_error(message_20::Type::CertificateInstallationReq, m_ctx);
        m_ctx.session_stopped = true;
        return {};
    }

    message_20::Header header;
    if (not validate_and_setup_header(header, m_ctx.session, req.header.session_id)) {
        respond_certificate_installation(req, dt::ResponseCode::FAILED_UnknownSession, dt::Processing::Finished);
        m_ctx.session_stopped = true;
        return {};
    }

    if (cert_install_raw_response.has_value()) {
        // The backend's signed CertificateInstallationRes goes out verbatim ([V2G20-1973/1975]).
        const auto& raw = *cert_install_raw_response;
        const io::StreamInputView view{raw.data(), raw.size()};
        message_20::Variant response(io::v2gtp::PayloadType::Part20Main, view);
        const auto* res = response.get_if<message_20::CertificateInstallationResponse>();
        if (res and res->header.session_id == m_ctx.session.get_id() and
            res->evse_processing == dt::Processing::Finished and
            m_ctx.respond_raw(raw, message_20::Type::CertificateInstallationRes)) {
            // [V2G20-2224]: an unknown prioritized eMSP permits a fresh installation attempt.
            if (res->response_code == dt::ResponseCode::WARNING_eMSPUnknown) {
                cert_install_remaining.reset();
            } else if (res->response_code == dt::ResponseCode::OK) {
                cert_install_remaining = res->remaining_contract_certificate_chains;
            } else {
                cert_install_remaining = 0;
            }
            if (res->response_code >= dt::ResponseCode::FAILED) {
                m_ctx.session_stopped = true;
            }
            cert_install_raw_response.reset();
            cert_install_forwarded = false;
            m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
            return {};
        }
        logf_warning("Authorization: invalid or oversized backend certificate response");
        cert_install_raw_response.reset();
        cert_install_failed = true;
    }

    if (cert_install_failed) {
        // [V2G20-2207], [V2G20-2225]
        m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
        timeout_ongoing_reached = false;
        cert_install_failed = false;
        cert_install_forwarded = false;
        cert_install_remaining = 0;
        respond_certificate_installation(req, dt::ResponseCode::WARNING_NoCertificateAvailable,
                                         dt::Processing::Finished);
        return {};
    }

    if (cert_install_forwarded) {
        // [V2G20-1972]: still waiting for the backend.
        respond_certificate_installation(req, dt::ResponseCode::OK, dt::Processing::Ongoing);
        return {};
    }

    const auto& chain = req.oem_provisioning_certificate_chain;
    // [V2G20-2203], [V2G20-2204]: "the SECC (or SA) deduces" an expired or not yet valid OEM provisioning
    // chain element. Trust of the chain stays with the certificate provisioning service ([V2G20-1548] NOTE 2).
    const std::vector<std::vector<uint8_t>> subs(chain.sub_certificates.begin(), chain.sub_certificates.end());
    if (const auto fault = crypto::chain_validity_fault(chain.certificate, subs)) {
        cert_install_remaining = 0;
        respond_certificate_installation(req, *fault, dt::Processing::Finished);
        return {};
    }
    const auto verdict = crypto::verify_signature(variant.get_exi_payload(), chain.certificate,
                                                  crypto::SignedElement::CertificateInstallationReq);
    if (verdict != crypto::SignatureVerdict::Ok) {
        // [V2G20-461]
        respond_certificate_installation(req, dt::ResponseCode::FAILED_SignatureError, dt::Processing::Finished);
        m_ctx.session_stopped = true;
        return {};
    }

    const auto& exi = variant.get_exi_payload();
    if (exi.empty()) {
        respond_certificate_installation(req, dt::ResponseCode::FAILED, dt::Processing::Finished);
        m_ctx.session_stopped = true;
        return {};
    }
    cert_install_forwarded = true;
    m_ctx.feedback.certificate_request(
        {base64_encode(exi), session::feedback::CertificateExchangeAction::Install, ProtocolId::ISO15118_20});
    m_ctx.restart_timeout(d20::TimeoutType::ONGOING, TIMEOUT_ONGOING);
    eim_timer_started = false;
    respond_certificate_installation(req, dt::ResponseCode::OK, dt::Processing::Ongoing);
    return {};
}

} // namespace iso15118::d20::state
