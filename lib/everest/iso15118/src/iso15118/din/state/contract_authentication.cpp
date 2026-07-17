// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/state/contract_authentication.hpp>

#include <iso15118/din/state/charge_parameter_discovery.hpp>
#include <iso15118/din/state/session_stop.hpp>

#include <iso15118/detail/din/state/contract_authentication.hpp>
#include <iso15118/detail/din/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::state {

message_din::ContractAuthenticationResponse handle_request(bool authorized, const dt::SessionId& session_id,
                                                           bool rejected) {
    message_din::ContractAuthenticationResponse res;
    setup_header(res.header, session_id);
    if (rejected) {
        // A rejected authorization must not spin Ongoing forever (EvseV2G din_server.cpp:482-489).
        res.evse_processing = dt::EvseProcessing::Finished;
        return response_with_code(res, dt::ResponseCode::FAILED);
    }
    res.evse_processing = authorized ? dt::EvseProcessing::Finished : dt::EvseProcessing::Ongoing;
    return response_with_code(res, dt::ResponseCode::OK);
}

void ContractAuthentication::enter() {
    m_ctx.log.enter_state("ContractAuthentication");
}

Result ContractAuthentication::feed(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control_data = m_ctx.get_control_event<d20::AuthorizationResponse>()) {
            m_ctx.authorized = static_cast<bool>(*control_data);
            auth_response_received = true;
        }
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    // An EV aborting mid-handshake sends SessionStopReq; hand it to SessionStop for a clean SessionStopRes.
    if (m_ctx.peek_request_type() == message_din::Type::SessionStopReq) {
        return m_ctx.create_state<SessionStop>();
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<message_din::ContractAuthenticationRequest>()) {
        // Signal the EIM authorization request once, when the first ContractAuthenticationReq arrives.
        if (not auth_requested) {
            m_ctx.feedback.signal(session::feedback::Signal::REQUIRE_AUTH_EIM);
            auth_requested = true;
        }

        if (req->header.session_id != m_ctx.get_session_id()) {
            message_din::ContractAuthenticationResponse res;
            setup_header(res.header, m_ctx.get_session_id());
            m_ctx.respond(response_with_code(res, dt::ResponseCode::FAILED_UnknownSession));
            m_ctx.session_stopped = true;
            return {};
        }

        const bool rejected = auth_response_received and not m_ctx.authorized;
        const auto res = handle_request(m_ctx.authorized, m_ctx.get_session_id(), rejected);
        m_ctx.respond(res);

        if (res.response_code >= dt::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
            return {};
        }

        if (res.evse_processing == dt::EvseProcessing::Finished) {
            return m_ctx.create_state<ChargeParameterDiscovery>();
        }
        return {};
    }

    m_ctx.log("expected ContractAuthenticationReq! But code type id: %d", variant->get_type());
    message_din::ContractAuthenticationResponse res;
    setup_header(res.header, m_ctx.get_session_id());
    m_ctx.respond(response_with_code(res, dt::ResponseCode::FAILED_SequenceError));
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::state
