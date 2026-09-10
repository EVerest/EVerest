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
// V2G_SECC_CableCheck_Performance_Time = 38 s (Table 111). The SECC bounds its cable-check
// (isolation-monitoring) phase 2 s below the EVCC's 40 s timeout so it gives up first.
constexpr uint32_t TIMEOUT_CABLE_CHECK_MS = 38000;
// V2G_SECC_Msg_Performance_Time(CableCheckRes) = 1,5 s (Table 109): the window
// [V2G2-916]..[V2G2-918] give the SECC to measure CP State C/D before it refuses the cable check.
constexpr uint32_t CP_STATE_PERFORMANCE_TIME_MS = 1500;
} // namespace

message_2::CableCheckResponse handle_request([[maybe_unused]] const message_2::CableCheckRequest& req,
                                             const dt::SessionId& session_id, bool cable_check_done,
                                             bool cable_check_fault,
                                             std::optional<dt::DC_EVSEStatusCode> error_status_code, bool charger_stop,
                                             std::optional<dt::IsolationLevel> reported_isolation) {
    message_2::CableCheckResponse res;
    res.header.session_id = session_id;

    res.dc_evse_status.notification = charger_stop ? dt::EVSENotification::StopCharging : dt::EVSENotification::None;
    res.dc_evse_status.notification_max_delay = 0;

    // The module's result takes precedence over the level derived from the cable check's own progress:
    // only the module can tell Warning, Fault and above all No_IMD apart, and it reports before
    // signalling cable_check_finished. An EVSE that never reports keeps the progress derivation.
    if (cable_check_fault) {
        // Answered with a negative CableCheckRes, then the session terminates (mirrors DIN [V2G-DC-890]).
        res.response_code = dt::ResponseCode::FAILED;
        res.dc_evse_status.isolation_status = reported_isolation.value_or(dt::IsolationLevel::Invalid);
        res.dc_evse_status.status_code = dt::DC_EVSEStatusCode::EVSE_Malfunction;
        res.evse_processing = dt::EVSEProcessing::Finished;
        return res;
    }

    res.response_code = dt::ResponseCode::OK;
    res.dc_evse_status.isolation_status =
        reported_isolation.value_or(cable_check_done ? dt::IsolationLevel::Valid : dt::IsolationLevel::Invalid);
    res.dc_evse_status.status_code = charger_stop       ? dt::DC_EVSEStatusCode::EVSE_Shutdown
                                     : cable_check_done ? dt::DC_EVSEStatusCode::EVSE_Ready
                                                        : dt::DC_EVSEStatusCode::EVSE_IsolationMonitoringActive;

    res.evse_processing = cable_check_done ? dt::EVSEProcessing::Finished : dt::EVSEProcessing::Ongoing;

    // So the EV sees the fault during isolation monitoring too; the isolation-fault path above returns
    // earlier and keeps its own EVSE_Malfunction.
    if (error_status_code.has_value()) {
        res.dc_evse_status.status_code = error_status_code.value();
    }
    return res;
}

void CableCheck::enter() {
    logf_debug("Enter state: CableCheck");
}

Result CableCheck::process_request(const message_2::CableCheckRequest& req) {
    if (not cable_check_initiated) {
        // On a renegotiation loop-back cable_check_done is already true: the contactor stayed closed
        // (8.7.4.3 NOTE 1), so isolation is still valid and a physical re-test is neither possible nor
        // required. Re-signalling START_CABLE_CHECK would drive EvseManager::cable_check(), which ABORTS
        // because the Charger has left PrepareCharging -- so skip the trigger and answer Finished at once.
        if (not m_ctx.evse().cable_check_done) {
            m_ctx.feedback.signal(session::feedback::Signal::START_CABLE_CHECK);
            m_ctx.start_timeout(d20::TimeoutType::ONGOING, TIMEOUT_CABLE_CHECK_MS);
        }
        cable_check_initiated = true;
    }

    const auto res = handle_request(req, m_ctx.get_session_id(), m_ctx.evse().cable_check_done,
                                    m_ctx.evse().cable_check_fault, m_ctx.error_status_code(),
                                    m_ctx.evse().charger_stop_requested, m_ctx.reported_isolation_level());
    m_ctx.respond(res);

    if (res.response_code >= dt::ResponseCode::FAILED) {
        m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
        m_ctx.session_stopped = true;
        return {};
    }

    if (res.evse_processing == dt::EVSEProcessing::Finished) {
        m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
        return m_ctx.create_state<PreChargeStart>();
    }

    return {};
}

Result CableCheck::on_event(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control = m_ctx.get_control_event<d20::CableCheckFinished>()) {
            // Absence of the event means the check is still ongoing.
            if (static_cast<bool>(*control)) {
                m_ctx.set_cable_check_done();
            } else {
                m_ctx.set_cable_check_fault();
            }
        }
        // Parked while waiting for CP State C/D ([V2G2-916]..[V2G2-918]): resume as soon as it arrives.
        if (pending_req.has_value() and
            (m_ctx.evse().current_cp_state == d20::CpState::C or m_ctx.evse().current_cp_state == d20::CpState::D)) {
            m_ctx.stop_timeout(d20::TimeoutType::CPSTATE);
            const auto req = pending_req.value();
            pending_req.reset();
            return process_request(req);
        }
        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto* timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::ONGOING) {
            logf_warning("CableCheck ongoing timeout reached, terminating session");
            m_ctx.session_stopped = true;
        } else if (timeout and *timeout == d20::TimeoutType::CPSTATE and pending_req.has_value()) {
            // [V2G2-918]: no CP State C/D in time, so the cable check must not run. The FAILED response arms
            // the FailedTermination path (oscillator off + SECC-side TCP close).
            logf_warning("no CP State C/D within V2G_SECC_Msg_Performance_Time, CableCheck -> FAILED");
            pending_req.reset();
            auto res = handle_request(message_2::CableCheckRequest{}, m_ctx.get_session_id(),
                                      /*cable_check_done=*/false, /*cable_check_fault=*/false,
                                      m_ctx.error_status_code(), m_ctx.evse().charger_stop_requested);
            res.response_code = dt::ResponseCode::FAILED;
            res.dc_evse_status.isolation_status = dt::IsolationLevel::Invalid;
            res.evse_processing = dt::EVSEProcessing::Finished;
            m_ctx.respond(res);
            m_ctx.session_stopped = true;
        }
        return {};
    }

    return {};
}

Result CableCheck::on_request(const message_2::Variant& received) {

    const auto req = received.get_if<message_2::CableCheckRequest>();
    if (req == nullptr) {
        logf_warning("Expected CableCheckReq! But code type id: %d", received.get_type());
        respond_sequence_error(m_ctx, received.get_type());
        return {};
    }

    m_ctx.report_ev_status(req->dc_ev_status);

    // [V2G2-916]..[V2G2-918]: on the initial cable check the EV must reach CP State C/D within the
    // performance time. A renegotiation loop-back keeps CP C/D and is not gated.
    if (not m_ctx.evse().cable_check_done and m_ctx.evse().current_cp_state != d20::CpState::C and
        m_ctx.evse().current_cp_state != d20::CpState::D) {
        pending_req = *req;
        m_ctx.arm_cp_state_timeout(CP_STATE_PERFORMANCE_TIME_MS);
        return {};
    }

    return process_request(*req);
}

void CableCheck::leave() {
    // Runs on every transition out, including the SessionStopReq abort that StateBase::feed() handles
    // without consulting this state, so no parked request's CP-state timeout survives into the next one.
    m_ctx.clear_cp_state_timeout();
}

} // namespace iso15118::d2::state
