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

namespace {
// [V2G2-712/713]: once the SECC has sent EVSEProcessing=Ongoing (or Ongoing_WaitingForCustomerInteraction)
// for the first time it starts the V2G_SECC_Ongoing_Timer; on reaching V2G_SECC_Ongoing_Performance_Time
// (55 s, Table 109) without Finished it terminates the session (best-effort FAILED on the next request).
constexpr uint32_t TIMEOUT_ONGOING_MS = 55000;
} // namespace

message_2::AuthorizationResponse handle_request([[maybe_unused]] const message_2::AuthorizationRequest& req,
                                                const dt::SessionId& session_id, bool authorized, bool timeout_reached,
                                                bool rejected, bool contract_selected) {
    message_2::AuthorizationResponse res;
    res.header.session_id = session_id;

    if (timeout_reached or rejected) {
        // A rejected authorization must not spin Ongoing forever (EvseV2G din_server.cpp:482-489).
        res.response_code = dt::ResponseCode::FAILED;
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
    m_ctx.log.enter_state("Authorization");
}

Result Authorization::feed(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control = m_ctx.get_control_event<d20::AuthorizationResponse>()) {
            m_ctx.authorized = static_cast<bool>(*control);
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

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    // An EV aborting mid-handshake sends SessionStopReq; hand it to SessionStop for a clean SessionStopRes.
    if (m_ctx.peek_request_type() == message_2::Type::SessionStopReq) {
        return m_ctx.create_state<SessionStop>();
    }

    const auto variant = m_ctx.pull_request();

    const auto req = variant->get_if<message_2::AuthorizationRequest>();
    if (req == nullptr) {
        m_ctx.log("expected AuthorizationReq! But code type id: %d", variant->get_type());
        // [V2G2-539]: answer with the received-type response carrying FAILED_SequenceError, then close.
        respond_sequence_error(m_ctx, *variant);
        m_ctx.session_stopped = true;
        return {};
    }

    // The request must echo the assigned SessionID [V2G2-388]; a mismatch is answered with
    // AuthorizationRes/FAILED_UnknownSession and terminates the session.
    if (reject_unknown_session(m_ctx, *variant)) {
        return {};
    }

    if (first_req_msg) {
        if (m_ctx.contract_selected) {
            // Plug-and-Charge [V2G2-684]: verify the GenChallenge echo and the AuthorizationReq signature
            // against the contract public key before requesting authorization from the higher layer.
            const bool challenge_ok =
                req->gen_challenge.has_value() and req->gen_challenge.value() == m_ctx.gen_challenge;
            if (not challenge_ok) {
                m_ctx.log("PnC Authorization: GenChallenge invalid or missing");
                message_2::AuthorizationResponse res;
                res.header.session_id = m_ctx.get_session_id();
                res.response_code = dt::ResponseCode::FAILED_ChallengeInvalid;
                res.evse_processing = dt::EVSEProcessing::Finished;
                m_ctx.respond(res);
                m_ctx.session_stopped = true;
                return {};
            }

            if (not crypto::verify_authorization_signature(variant->get_exi_payload(), m_ctx.contract_leaf_der)) {
                m_ctx.log("PnC Authorization: signature verification failed");
                message_2::AuthorizationResponse res;
                res.header.session_id = m_ctx.get_session_id();
                res.response_code = dt::ResponseCode::FAILED_SignatureError;
                res.evse_processing = dt::EVSEProcessing::Finished;
                m_ctx.respond(res);
                m_ctx.session_stopped = true;
                return {};
            }

            // Signature verified: request PnC authorization for the contract eMAID from the higher layer.
            m_ctx.feedback.require_auth_pnc(m_ctx.contract_emaid, m_ctx.contract_chain_pem);
        } else {
            m_ctx.feedback.signal(session::feedback::Signal::REQUIRE_AUTH_EIM);
        }
        m_ctx.start_timeout(d20::TimeoutType::ONGOING, TIMEOUT_ONGOING_MS);
        first_req_msg = false;
    }

    const bool rejected = auth_response_received and not m_ctx.authorized;
    const auto res = handle_request(*req, m_ctx.get_session_id(), m_ctx.authorized, timeout_ongoing_reached, rejected,
                                    m_ctx.contract_selected);
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
