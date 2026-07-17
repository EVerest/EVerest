// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/ev/state/contract_authentication.hpp>

#include <iso15118/din/ev/state/charge_parameter_discovery.hpp>
#include <iso15118/din/ev/timeouts.hpp>

#include <iso15118/detail/din/ev/state/contract_authentication.hpp>
#include <iso15118/detail/din/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::ev::state {

namespace contract_authentication {

message_din::ContractAuthenticationRequest create_request() {
    return {}; // EIM: id and gen_challenge omitted
}

Result handle_response(const message_din::ContractAuthenticationResponse& res) {
    if (res.response_code >= dt::ResponseCode::FAILED) {
        return {Action::Failed};
    }
    if (res.evse_processing == dt::EvseProcessing::Finished) {
        return {Action::Done};
    }
    return {Action::Retry};
}

} // namespace contract_authentication

using namespace contract_authentication;

void ContractAuthentication::enter() {
    m_ctx.log.enter_state("ContractAuthentication");
}

void ContractAuthentication::send(Event) {
    if (first_request) {
        m_ctx.start_timeout(d20::TimeoutType::ONGOING, TIMEOUT_CONTRACT_AUTHENTICATION_ONGOING_MS);
        first_request = false;
    }

    auto req = create_request();
    m_ctx.setup_header(req.header);
    m_ctx.send_request(req);
    m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_MS);
}

din::ev::Result ContractAuthentication::feed(Event ev) {
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
            m_ctx.log("ContractAuthentication ongoing timeout reached, terminating session");
            ongoing_timeout_reached = true;
        } else {
            m_ctx.log("ContractAuthentication message timeout reached, terminating session");
        }
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_din::ContractAuthenticationResponse>()) {
        const auto result = handle_response(*res);

        switch (result.action) {
        case Action::Failed:
            m_ctx.log("ContractAuthentication failed, terminating session");
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

    m_ctx.log("expected ContractAuthenticationRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::ev::state
