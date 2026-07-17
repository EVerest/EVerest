// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/cable_check.hpp>

#include <iso15118/d2/state/pre_charge.hpp>
#include <iso15118/d2/state/session_stop.hpp>

#include <iso15118/detail/d2/state/cable_check.hpp>
#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

namespace {
constexpr uint32_t TIMEOUT_CABLE_CHECK_MS = 40000;
} // namespace

message_2::CableCheckResponse handle_request([[maybe_unused]] const message_2::CableCheckRequest& req,
                                             const dt::SessionId& session_id, bool cable_check_done) {
    message_2::CableCheckResponse res;
    res.header.session_id = session_id;
    res.response_code = dt::ResponseCode::OK;

    res.dc_evse_status.notification = dt::EVSENotification::None;
    res.dc_evse_status.notification_max_delay = 0;
    res.dc_evse_status.isolation_status = cable_check_done ? dt::IsolationLevel::Valid : dt::IsolationLevel::Invalid;
    res.dc_evse_status.status_code =
        cable_check_done ? dt::DC_EVSEStatusCode::EVSE_Ready : dt::DC_EVSEStatusCode::EVSE_IsolationMonitoringActive;

    res.evse_processing = cable_check_done ? dt::EVSEProcessing::Finished : dt::EVSEProcessing::Ongoing;
    return res;
}

void CableCheck::enter() {
    m_ctx.log.enter_state("CableCheck");
}

Result CableCheck::feed(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control = m_ctx.get_control_event<d20::CableCheckFinished>()) {
            m_ctx.cable_check_done = static_cast<bool>(*control);
        }
        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto* timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::ONGOING) {
            m_ctx.log("CableCheck ongoing timeout reached, terminating session");
            m_ctx.session_stopped = true;
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

    const auto req = variant->get_if<message_2::CableCheckRequest>();
    if (req == nullptr) {
        m_ctx.log("expected CableCheckReq! But code type id: %d", variant->get_type());
        // [V2G2-539]: answer with the received-type response carrying FAILED_SequenceError, then close.
        respond_sequence_error(m_ctx, *variant);
        m_ctx.session_stopped = true;
        return {};
    }

    // The request must echo the assigned SessionID [V2G2-388]; a mismatch is answered with
    // CableCheckRes/FAILED_UnknownSession and terminates the session.
    if (reject_unknown_session(m_ctx, *variant)) {
        return {};
    }

    if (not cable_check_initiated) {
        m_ctx.feedback.signal(session::feedback::Signal::START_CABLE_CHECK);
        m_ctx.start_timeout(d20::TimeoutType::ONGOING, TIMEOUT_CABLE_CHECK_MS);
        cable_check_initiated = true;
    }

    const auto res = handle_request(*req, m_ctx.get_session_id(), m_ctx.cable_check_done);
    m_ctx.respond(res);

    if (res.evse_processing == dt::EVSEProcessing::Finished) {
        m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
        return m_ctx.create_state<PreCharge>();
    }

    return {};
}

} // namespace iso15118::d2::state
