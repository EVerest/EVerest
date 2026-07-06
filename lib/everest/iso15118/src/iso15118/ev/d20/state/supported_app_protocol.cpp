// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d20/state/supported_app_protocol.hpp>

#include <iso15118/ev/d20/state/session_setup.hpp>

#include <iso15118/message/supported_app_protocol.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>

namespace iso15118::ev::d20::state {

using ResponseCode = message_20::SupportedAppProtocolResponse::ResponseCode;

void SupportedAppProtocol::enter() {
    m_ctx.log.enter_state("SupportedAppProtocol");

    message_20::SupportedAppProtocolRequest req{};
    for (const auto& ap : m_ctx.get_advertised_app_protocols()) {
        req.app_protocol.push_back(ap);
    }

    m_ctx.send_request(req);
}

Result SupportedAppProtocol::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    auto variant = m_ctx.pull_response();

    // SupportedAppProtocolResponse carries no session id and its own response-code
    // enum, so it is validated inline rather than through expect_response.
    const auto* res = variant->get_if<message_20::SupportedAppProtocolResponse>();
    if (res == nullptr) {
        logf_error("expected SupportedAppProtocolRes, but got message type id: %d",
                   static_cast<int>(variant->get_type()));
        m_ctx.stop_session();
        return {};
    }

    if (res->response_code != ResponseCode::OK_SuccessfulNegotiation and
        res->response_code != ResponseCode::OK_SuccessfulNegotiationWithMinorDeviation) {
        logf_error("SupportedAppProtocol negotiation failed with response code: %d",
                   static_cast<int>(res->response_code));
        m_ctx.stop_session();
        return {};
    }

    // Deferred seam: the negotiated schema_id selects the protocol the rest of the
    // FSM should speak (d2/DIN vs -20). Only -20 is wired today, so we unconditionally
    // proceed into the -20 SessionSetup, whose enter() sends the SessionSetupRequest.
    return m_ctx.create_state<SessionSetup>();
}

} // namespace iso15118::ev::d20::state
