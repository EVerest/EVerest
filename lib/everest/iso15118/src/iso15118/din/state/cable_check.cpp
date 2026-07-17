// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/state/cable_check.hpp>

#include <iso15118/din/state/pre_charge.hpp>
#include <iso15118/din/state/session_stop.hpp>

#include <iso15118/detail/din/state/cable_check.hpp>
#include <iso15118/detail/din/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::state {

message_din::CableCheckResponse handle_request(const message_din::CableCheckRequest& req, bool cable_check_done,
                                               const dt::SessionId& session_id) {
    message_din::CableCheckResponse res;

    if (not validate_and_setup_header(res.header, session_id, req.header.session_id)) {
        return response_with_code(res, dt::ResponseCode::FAILED_UnknownSession);
    }

    if (cable_check_done) {
        res.evse_processing = dt::EvseProcessing::Finished;
        // [V2G-DC-499] The EVCC proceeds to PreCharge only on EVSE_Ready with a Valid/Warning isolation.
        res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
        res.dc_evse_status.evse_isolation_status = dt::IsolationLevel::Valid;
    } else {
        res.evse_processing = dt::EvseProcessing::Ongoing;
        res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_IsolationMonitoringActive;
    }

    return response_with_code(res, dt::ResponseCode::OK);
}

void CableCheck::enter() {
    m_ctx.log.enter_state("CableCheck");
}

Result CableCheck::feed(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control_data = m_ctx.get_control_event<d20::CableCheckFinished>()) {
            m_ctx.cable_check_done = static_cast<bool>(*control_data);
        }
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    // An EV aborting sends SessionStopReq; hand it to SessionStop for a clean SessionStopRes.
    if (m_ctx.peek_request_type() == message_din::Type::SessionStopReq) {
        return m_ctx.create_state<SessionStop>();
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<message_din::CableCheckRequest>()) {
        if (not cable_check_initiated) {
            m_ctx.feedback.signal(session::feedback::Signal::START_CABLE_CHECK);
            cable_check_initiated = true;
        }

        const auto res = handle_request(*req, m_ctx.cable_check_done, m_ctx.get_session_id());
        m_ctx.respond(res);

        if (res.response_code >= dt::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
            return {};
        }

        if (m_ctx.cable_check_done) {
            return m_ctx.create_state<PreCharge>();
        }
        return {};
    }

    m_ctx.log("expected CableCheckReq! But code type id: %d", variant->get_type());
    message_din::CableCheckResponse res;
    setup_header(res.header, m_ctx.get_session_id());
    m_ctx.respond(response_with_code(res, dt::ResponseCode::FAILED_SequenceError));
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::state
