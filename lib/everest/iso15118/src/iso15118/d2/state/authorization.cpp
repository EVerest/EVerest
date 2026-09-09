// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/authorization.hpp>
#include <iso15118/d2/state/charge_parameter_discovery.hpp>
#include <iso15118/detail/d2/context_helper.hpp>
#include <iso15118/detail/helper.hpp>
#include <iso15118/message/d2/authorization.hpp>
#include <iso15118/session/feedback.hpp>

namespace iso15118::d2::state {

namespace dt = msg::data_types;

void Authorization::enter() {
    // Signal EVSE controller that EIM authorization is required
    m_ctx.feedback.signal(session::feedback::Signal::REQUIRE_AUTH_EIM);
}

Result Authorization::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<msg::AuthorizationRequest>()) {
        msg::AuthorizationResponse res;
        setup_header(res.header, m_ctx.session);
        // EIM: gate on external authorization (RFID / remote) via the feedback
        // callback. While not authorized, report EVSEProcessing=Ongoing so the EV
        // keeps polling AuthorizationReq (the proper "waiting for the swipe" loop,
        // matching the field-proven EvseV2G behavior). Once authorized, report
        // Finished and advance. If no callback is wired, is_authorized() defaults
        // to true (legacy immediate grant).
        if (m_ctx.feedback.is_authorized()) {
            res.evse_processing = dt::EvseProcessing::Finished;
            response_with_code(res, dt::ResponseCode::OK);
            m_ctx.respond(res);
            return m_ctx.create_state<ChargeParameterDiscovery>();
        } else {
            res.evse_processing = dt::EvseProcessing::Ongoing;
            response_with_code(res, dt::ResponseCode::OK);
            m_ctx.respond(res);
            return {}; // stay in Authorization; EV re-polls AuthorizationReq
        }
    }

    const msg::Type req_type = variant->get_type();
    send_sequence_error(req_type, m_ctx);
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d2::state
