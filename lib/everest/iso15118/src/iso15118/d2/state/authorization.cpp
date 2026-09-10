// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/authorization.hpp>

#include <iso15118/d2/state/charge_parameter_discovery.hpp>
#include <iso15118/d2/state/session_stop.hpp>

#include <iso15118/detail/d2/crypto.hpp>
#include <iso15118/detail/d2/state/authorization.hpp>
#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

message_2::AuthorizationResponse handle_request([[maybe_unused]] const message_2::AuthorizationRequest& req,
                                                const dt::SessionId& session_id, bool authorized, bool timeout_reached,
                                                bool rejected, bool contract_selected, bool certificate_revoked) {
    message_2::AuthorizationResponse res;
    res.header.session_id = session_id;

    if (timeout_reached or rejected) {
        // A rejected authorization must not spin Ongoing forever. A PnC rejection due to a revoked contract
        // certificate names the reason [V2G2-475]; anything else stays a plain FAILED.
        res.response_code = (rejected and contract_selected and certificate_revoked)
                                ? dt::ResponseCode::FAILED_CertificateRevoked
                                : dt::ResponseCode::FAILED;
        res.evse_processing = dt::EVSEProcessing::Finished;
        return res;
    }

    res.response_code = dt::ResponseCode::OK;
    if (authorized) {
        // [V2G2-856] positive authorization (EIM or PnC) -> Finished.
        res.evse_processing = dt::EVSEProcessing::Finished;
    } else {
        // Authorization still pending: [V2G2-855] PnC (Contract) -> Ongoing;
        // [V2G2-854] EIM (External Payment) -> Ongoing_WaitingForCustomerInteraction.
        res.evse_processing =
            contract_selected ? dt::EVSEProcessing::Ongoing : dt::EVSEProcessing::Ongoing_WaitingForCustomerInteraction;
    }
    return res;
}

void Authorization::enter() {
    logf_debug("Enter state: Authorization");
}

Result Authorization::on_event(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control = m_ctx.get_control_event<d20::AuthorizationResponse>()) {
            authorized = static_cast<bool>(*control);
            certificate_revoked = control->is_certificate_revoked();
            auth_response_received = true;
        }
        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto* timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::ONGOING) {
            timeout_ongoing_reached = true;
        }
        return {};
    }

    return {};
}

Result Authorization::on_request(const message_2::Variant& received) {

    const auto req = received.get_if<message_2::AuthorizationRequest>();
    if (req == nullptr) {
        logf_warning("Expected AuthorizationReq! But code type id: %d", received.get_type());
        respond_sequence_error(m_ctx, received.get_type());
        return {};
    }

    if (first_req_msg) {
        if (m_ctx.session().contract_selected) {
            // [V2G2-684]: verify the GenChallenge echo and the AuthorizationReq signature before requesting
            // authorization. The challenge is the one PaymentDetails handed over, not shared session state.
            const bool challenge_ok = req->gen_challenge.has_value() and gen_challenge.has_value() and
                                      req->gen_challenge.value() == gen_challenge.value();
            if (not challenge_ok) {
                logf_warning("PnC Authorization: GenChallenge invalid or missing");
                message_2::AuthorizationResponse res;
                res.header.session_id = m_ctx.get_session_id();
                res.response_code = dt::ResponseCode::FAILED_ChallengeInvalid;
                res.evse_processing = dt::EVSEProcessing::Finished;
                m_ctx.respond(res);
                m_ctx.session_stopped = true;
                return {};
            }

            if (not crypto::verify_authorization_signature(received.get_exi_payload(),
                                                           m_ctx.session().contract_leaf_der)) {
                logf_warning("PnC Authorization: signature verification failed");
                message_2::AuthorizationResponse res;
                res.header.session_id = m_ctx.get_session_id();
                res.response_code = dt::ResponseCode::FAILED_SignatureError;
                res.evse_processing = dt::EVSEProcessing::Finished;
                m_ctx.respond(res);
                m_ctx.session_stopped = true;
                return {};
            }

            m_ctx.feedback.require_auth_pnc(m_ctx.session().contract_emaid, m_ctx.session().contract_chain_pem);
        } else {
            m_ctx.feedback.signal(session::feedback::Signal::REQUIRE_AUTH_EIM);
        }
        // [V2G2-712/713]: the V2G_SECC_Ongoing_Timer starts with the first Ongoing response and terminates
        // the session on expiry. Configurable per payment option: V2G_SECC_Ongoing_Performance_Time (55 s) is
        // the PnC default, while EIM defaults far higher because a human has to act. 0 waits indefinitely.
        const auto timeout_ms = m_ctx.session().contract_selected ? m_ctx.session_config.auth_timeout_pnc_ms
                                                                  : m_ctx.session_config.auth_timeout_eim_ms;
        if (timeout_ms > 0) {
            m_ctx.start_timeout(d20::TimeoutType::ONGOING, timeout_ms);
        } else {
            logf_debug("Authorization: waiting for the authorization result indefinitely (timeout disabled)");
        }
        first_req_msg = false;
    }

    const bool rejected = auth_response_received and not authorized;
    const auto res = handle_request(*req, m_ctx.get_session_id(), authorized, timeout_ongoing_reached, rejected,
                                    m_ctx.session().contract_selected, certificate_revoked);
    m_ctx.respond(res);

    if (res.response_code >= dt::ResponseCode::FAILED) {
        m_ctx.session_stopped = true;
        return {};
    }

    if (res.evse_processing == dt::EVSEProcessing::Finished) {
        m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
        return m_ctx.create_state<ChargeParameterDiscovery>();
    }

    return {};
}

} // namespace iso15118::d2::state
