// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/ev/state/authorization.hpp>

#include <iso15118/d2/ev/state/charge_parameter_discovery.hpp>
#include <iso15118/d2/ev/timeouts.hpp>

#include <iso15118/detail/d2/crypto.hpp>
#include <iso15118/detail/d2/ev/state/authorization.hpp>
#include <iso15118/detail/d2/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::ev::state {

namespace authorization {

message_2::AuthorizationRequest create_request() {
    return {}; // EIM: id and gen_challenge omitted
}

Result handle_response(const message_2::AuthorizationResponse& res) {
    if (res.response_code >= dt::ResponseCode::FAILED) {
        return {Action::Failed};
    }
    if (res.evse_processing == dt::EVSEProcessing::Finished) {
        return {Action::Done};
    }
    return {Action::Retry};
}

} // namespace authorization

using namespace authorization;

void Authorization::enter() {
    m_ctx.log.enter_state("Authorization");
}

void Authorization::send(Event) {
    if (first_request) {
        m_ctx.start_timeout(d20::TimeoutType::ONGOING, TIMEOUT_ONGOING_MS);
        first_request = false;
    }

    if (m_ctx.pnc.contract_selected) {
        // Plug-and-Charge: the AuthorizationReq echoes the GenChallenge from PaymentDetailsRes and is
        // signed with the contract certificate key [V2G2-684].
        message_2::AuthorizationRequest req;
        req.id = "id1";
        req.gen_challenge = m_ctx.pnc.gen_challenge;
        m_ctx.setup_header(req.header);
        const crypto::PrivateKey key{m_ctx.pnc.contract_key_pem, m_ctx.pnc.contract_key_password};
        const auto signed_exi = crypto::serialize_signed(req, key);
        if (signed_exi.empty()) {
            m_ctx.log("Authorization: failed to sign the PnC AuthorizationReq, terminating session");
            m_ctx.session_stopped = true;
            return;
        }
        m_ctx.send_raw(signed_exi, message_2::Type::AuthorizationReq);
    } else {
        auto req = create_request();
        m_ctx.setup_header(req.header);
        m_ctx.send_request(req);
    }
    m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_MS);
}

d2::ev::Result Authorization::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        send(ev);
        return {};
    }

    if (ev == Event::CONTROL_MESSAGE) {
        handle_stop_control_event(m_ctx);
        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto* timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::ONGOING) {
            m_ctx.log("Authorization ongoing timeout reached, terminating session");
            ongoing_timeout_reached = true;
        } else {
            m_ctx.log("Authorization message timeout reached, terminating session");
        }
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_2::AuthorizationResponse>()) {
        const auto result = handle_response(*res);

        switch (result.action) {
        case Action::Failed:
            m_ctx.log("Authorization failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        case Action::Done:
            m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
            if (auto stop = stop_if_pending(m_ctx)) {
                return stop;
            }
            return m_ctx.create_state<ChargeParameterDiscovery>();
        case Action::Retry:
        default:
            if (ongoing_timeout_reached) {
                m_ctx.session_stopped = true;
                return {};
            }
            send(ev);
            return {};
        }
    }

    m_ctx.log("expected AuthorizationRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d2::ev::state
