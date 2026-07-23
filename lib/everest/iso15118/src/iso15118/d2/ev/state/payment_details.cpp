// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/ev/state/payment_details.hpp>

#include <iso15118/d2/ev/state/authorization.hpp>
#include <iso15118/d2/ev/timeouts.hpp>

#include <iso15118/detail/d2/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>
#include <iso15118/message_2/payment_details.hpp>

namespace iso15118::d2::ev::state {

void PaymentDetails::enter() {
    m_ctx.log.enter_state("PaymentDetails");
}

d2::ev::Result PaymentDetails::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        message_2::PaymentDetailsRequest req;
        m_ctx.setup_header(req.header);
        req.emaid = m_ctx.pnc.emaid;
        req.contract_certificate = m_ctx.pnc.contract_cert_der;
        req.sub_certificates = m_ctx.pnc.contract_sub_certs_der;
        m_ctx.send_request(req);
        m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_MS);
        return {};
    }

    if (ev == Event::CONTROL_MESSAGE) {
        handle_stop_control_event(m_ctx);
        return {};
    }

    if (ev == Event::TIMEOUT) {
        m_ctx.log("PaymentDetails message timeout reached, terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_2::PaymentDetailsResponse>()) {
        if (res->response_code >= dt::ResponseCode::FAILED) {
            m_ctx.log("PaymentDetails failed (response code %d), terminating session", res->response_code);
            m_ctx.session_stopped = true;
            return {};
        }

        // Store the GenChallenge to echo (and sign over) in the AuthorizationReq [V2G2-684].
        m_ctx.pnc.gen_challenge = res->gen_challenge;

        if (auto stop = stop_if_pending(m_ctx)) {
            return stop;
        }
        return m_ctx.create_state<Authorization>();
    }

    m_ctx.log("expected PaymentDetailsRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d2::ev::state
