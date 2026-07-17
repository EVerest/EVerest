// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/pre_charge.hpp>

#include <iso15118/d2/state/power_delivery.hpp>
#include <iso15118/d2/state/session_stop.hpp>

#include <iso15118/message/common_types.hpp>
#include <iso15118/message/dc_charge_loop.hpp>

#include <iso15118/detail/d2/state/pre_charge.hpp>
#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/d2/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

namespace {
constexpr uint32_t TIMEOUT_PRE_CHARGE_MS = 50000;

namespace m20dt = message_20::datatypes;
} // namespace

message_2::PreChargeResponse handle_request([[maybe_unused]] const message_2::PreChargeRequest& req,
                                            const dt::SessionId& session_id, float present_voltage) {
    message_2::PreChargeResponse res;
    res.header.session_id = session_id;
    res.response_code = dt::ResponseCode::OK;

    res.dc_evse_status.notification = dt::EVSENotification::None;
    res.dc_evse_status.notification_max_delay = 0;
    res.dc_evse_status.isolation_status = dt::IsolationLevel::Valid;
    res.dc_evse_status.status_code = dt::DC_EVSEStatusCode::EVSE_Ready;

    res.evse_present_voltage = dt::to_physical_value(present_voltage, dt::Unit::V);
    return res;
}

void PreCharge::enter() {
    m_ctx.log.enter_state("PreCharge");
}

Result PreCharge::feed(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control = m_ctx.get_control_event<d20::PresentVoltageCurrent>()) {
            m_ctx.present_voltage = control->voltage;
            m_ctx.present_current = control->current;
        }
        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto* timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::ONGOING) {
            m_ctx.log("PreCharge ongoing timeout reached, terminating session");
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

    // The pre-charge loop ends when the EV converges and sends PowerDeliveryReq; hand it to the
    // PowerDelivery state (transition without consuming; the engine re-feeds it).
    if (m_ctx.peek_request_type() != message_2::Type::PreChargeReq) {
        return m_ctx.create_state<PowerDelivery>();
    }

    const auto variant = m_ctx.pull_request();
    const auto req = variant->get<message_2::PreChargeRequest>();

    // The request must echo the assigned SessionID [V2G2-388]; a mismatch is answered with
    // PreChargeRes/FAILED_UnknownSession and terminates the session.
    if (reject_unknown_session(m_ctx, *variant)) {
        return {};
    }

    if (not pre_charge_initiated) {
        m_ctx.feedback.signal(session::feedback::Signal::PRE_CHARGE_STARTED);
        m_ctx.start_timeout(d20::TimeoutType::ONGOING, TIMEOUT_PRE_CHARGE_MS);
        pre_charge_initiated = true;
    }

    // Report both the EV pre-charge target voltage and current so the power supply can follow the EV
    // (without the current the supply would stay at 0 A). Uses the dc_charge_loop_req scheduled-mode path.
    {
        m20dt::Scheduled_DC_CLReqControlMode mode{};
        mode.target_voltage = m20dt::from_float(static_cast<float>(dt::from_physical_value(req.ev_target_voltage)));
        mode.target_current = m20dt::from_float(static_cast<float>(dt::from_physical_value(req.ev_target_current)));
        m_ctx.feedback.dc_charge_loop_req(session::feedback::DcReqControlMode{mode});
    }

    const auto res = handle_request(req, m_ctx.get_session_id(), m_ctx.present_voltage);
    m_ctx.respond(res);

    return {};
}

} // namespace iso15118::d2::state
