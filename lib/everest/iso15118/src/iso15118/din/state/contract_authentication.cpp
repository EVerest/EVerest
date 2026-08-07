// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/state/contract_authentication.hpp>

#include <iso15118/din/state/charge_parameter_discovery.hpp>
#include <iso15118/din/state/session_stop.hpp>

#include <iso15118/detail/din/state/contract_authentication.hpp>
#include <iso15118/detail/din/state/sequence_error.hpp>
#include <iso15118/detail/din/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::state {

message_din::ContractAuthenticationResponse handle_request(bool authorized, const dt::SessionId& session_id,
                                                           bool rejected, bool timeout_reached) {
    message_din::ContractAuthenticationResponse res;
    setup_header(res.header, session_id);
    if (rejected or timeout_reached) {
        // A rejected authorization must not spin Ongoing forever (EvseV2G din_server.cpp:482-489), and
        // neither may one that never gets an answer at all (see the auth timeout in feed()).
        res.evse_processing = dt::EvseProcessing::Finished;
        return response_with_code(res, dt::ResponseCode::FAILED);
    }
    res.evse_processing = authorized ? dt::EvseProcessing::Finished : dt::EvseProcessing::Ongoing;
    return response_with_code(res, dt::ResponseCode::OK);
}

void ContractAuthentication::enter() {
    logf_debug("Enter state: ContractAuthentication");
}

Result ContractAuthentication::feed(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control_data = m_ctx.get_control_event<d20::AuthorizationResponse>()) {
            m_ctx.authorized = static_cast<bool>(*control_data);
            auth_response_received = true;
        }
        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto* timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::ONGOING) {
            logf_warning("No authorization result within the configured timeout, ContractAuthentication -> FAILED");
            timeout_ongoing_reached = true;
        }
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    // An EV aborting mid-handshake sends SessionStopReq; hand it to SessionStop for a clean SessionStopRes.
    if (m_ctx.peek_request_type() == message_din::Type::SessionStopReq) {
        // Release the ONGOING slot: the states taking over arm their own windows on it and
        // Timeouts::start_timeout() refuses an already-occupied slot.
        m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
        return m_ctx.create_state<SessionStop>();
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<message_din::ContractAuthenticationRequest>()) {
        // [V2G-DC-391]: validate the SessionID before any side effect -- a request from an unknown
        // session must not trigger the charger-side EIM authorization flow.
        if (reject_unknown_session(m_ctx, *variant)) {
            return {};
        }

        // Signal the EIM authorization request once, when the first ContractAuthenticationReq arrives,
        // and bound the Ongoing loop from that same moment. DIN SPEC 70121 defines no timer for this
        // (and EvseV2G left it unbounded), so the window is the module's auth_timeout_eim; 0 keeps
        // EvseV2G's wait-forever behaviour.
        if (not auth_requested) {
            m_ctx.feedback.signal(session::feedback::Signal::REQUIRE_AUTH_EIM);
            auth_requested = true;
            const auto timeout_ms = m_ctx.session_config.auth_timeout_eim_ms;
            if (timeout_ms > 0) {
                m_ctx.start_timeout(d20::TimeoutType::ONGOING, timeout_ms);
            } else {
                logf_debug("ContractAuthentication: waiting for the authorization result indefinitely (timeout "
                           "disabled)");
            }
        }

        const bool rejected = auth_response_received and not m_ctx.authorized;
        const auto res = handle_request(m_ctx.authorized, m_ctx.get_session_id(), rejected, timeout_ongoing_reached);
        m_ctx.respond(res);

        if (res.response_code >= dt::ResponseCode::FAILED) {
            m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
            m_ctx.session_stopped = true;
            return {};
        }

        if (res.evse_processing == dt::EvseProcessing::Finished) {
            // Release the ONGOING slot before the charge-loop states arm their own windows on it.
            m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
            return m_ctx.create_state<ChargeParameterDiscovery>();
        }
        return {};
    }

    logf_warning("Expected ContractAuthenticationReq! But code type id: %d", variant->get_type());
    // [V2G-DC-539]: answer with the received-type response carrying FAILED_SequenceError, then close.
    respond_sequence_error(m_ctx, *variant);
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::state
