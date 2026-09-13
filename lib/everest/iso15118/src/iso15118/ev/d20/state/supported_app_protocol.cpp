// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d20/state/supported_app_protocol.hpp>

#include <algorithm>

#include <iso15118/ev/d20/state/session_setup.hpp>

#include <iso15118/message/supported_app_protocol.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>

namespace iso15118::ev::d20::state {

using ResponseCode = message_20::SupportedAppProtocolResponse::ResponseCode;

void SupportedAppProtocol::enter() {
    logf_debug("Enter state: SupportedAppProtocol");

    message_20::SupportedAppProtocolRequest req{};
    for (const auto& ap : m_ctx.get_advertised_app_protocols()) {
        req.app_protocol.push_back(ap);
    }

    m_ctx.send_request(req);
}

Result SupportedAppProtocol::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    auto variant = m_ctx.pull_response();

    // SupportedAppProtocolResponse carries no session id and its own response-code
    // enum, so it is validated inline rather than through expect_response.
    const auto* res = variant->get_if<message_20::SupportedAppProtocolResponse>();
    if (res == nullptr) {
        logf_error("expected SupportedAppProtocolRes, but got message type id: %d",
                   static_cast<int>(variant->get_type()));
        m_ctx.stop_session();
        return Result::stopping();
    }

    if (res->response_code != ResponseCode::OK_SuccessfulNegotiation and
        res->response_code != ResponseCode::OK_SuccessfulNegotiationWithMinorDeviation) {
        logf_error("SupportedAppProtocol negotiation failed with response code: %d",
                   static_cast<int>(res->response_code));
        m_ctx.stop_session();
        return Result::stopping();
    }

    // schema_id -> protocol generation. An empty map means every offered entry was ISO 15118-20.
    auto protocol = ProtocolId::ISO15118_20;
    const auto& offered = m_ctx.options().offered_protocols;
    if (not offered.empty()) {
        if (not res->schema_id.has_value()) {
            logf_error("SupportedAppProtocolRes accepted the negotiation but carries no schema_id");
            m_ctx.stop_session();
            return Result::stopping();
        }
        const auto it = std::find_if(offered.begin(), offered.end(),
                                     [&](const auto& o) { return o.entry.schema_id == res->schema_id; });
        if (it == offered.end()) {
            logf_error("SupportedAppProtocolRes selected schema_id %d which was not offered",
                       static_cast<int>(res->schema_id.value()));
            m_ctx.stop_session();
            return Result::stopping();
        }
        protocol = it->protocol;
    }

    m_ctx.set_negotiated_protocol(protocol);
    m_ctx.feedback.selected_protocol(protocol);

    if (protocol != ProtocolId::ISO15118_20) {
        return Result::handover();
    }
    return m_ctx.create_state<SessionSetup>();
}

} // namespace iso15118::ev::d20::state
