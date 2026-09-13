// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/state/cable_check.hpp>

#include <iso15118/din/state/pre_charge.hpp>
#include <iso15118/din/state/session_stop.hpp>

#include <iso15118/detail/din/state/cable_check.hpp>
#include <iso15118/detail/din/state/constants.hpp>
#include <iso15118/detail/din/state/sequence_error.hpp>
#include <iso15118/detail/din/state/session_stop.hpp>
#include <iso15118/detail/din/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::state {

message_din::CableCheckResponse handle_request([[maybe_unused]] const message_din::CableCheckRequest& req,
                                               bool cable_check_done, bool cable_check_fault,
                                               const dt::SessionId& session_id,
                                               std::optional<dt::DcEvseStatusCode> error_status_code, bool charger_stop,
                                               std::optional<dt::IsolationLevel> reported_isolation) {
    message_din::CableCheckResponse res;
    setup_header(res.header, session_id);

    // Mandatory in CableCheckRes even on a FAILED_UnknownSession response, so default it before the check.
    res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
    res.dc_evse_status.evse_isolation_status = dt::IsolationLevel::Valid;
    // The module's result takes precedence over the level derived from the cable check's own progress:
    // only the module can tell Warning and Fault apart, and it reports before signalling
    // cable_check_finished. No_IMD cannot reach here -- DIN's isolationLevelType has no such enumerator.
    if (cable_check_fault) {
        // [V2G-DC-890]: an isolation fault is answered with a negative CableCheckRes, then the session ends.
        res.evse_processing = dt::EvseProcessing::Finished;
        res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Malfunction;
        res.dc_evse_status.evse_isolation_status = reported_isolation.value_or(dt::IsolationLevel::Invalid);
        return response_with_code(res, dt::ResponseCode::FAILED);
    }

    if (cable_check_done) {
        res.evse_processing = dt::EvseProcessing::Finished;
        // [V2G-DC-499] The EVCC proceeds to PreCharge only on EVSE_Ready with a Valid/Warning isolation.
        res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
        res.dc_evse_status.evse_isolation_status = reported_isolation.value_or(dt::IsolationLevel::Valid);
    } else {
        res.evse_processing = dt::EvseProcessing::Ongoing;
        res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_IsolationMonitoringActive;
        if (reported_isolation.has_value()) {
            res.dc_evse_status.evse_isolation_status = reported_isolation.value();
        }
    }

    if (charger_stop) {
        res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Shutdown;
    }

    // So the EV sees the fault during isolation monitoring too; the fault path above returns earlier.
    if (error_status_code.has_value()) {
        res.dc_evse_status.evse_status_code = error_status_code.value();
    }

    return response_with_code(res, dt::ResponseCode::OK);
}

void CableCheck::enter() {
    logf_debug("Enter state: CableCheck");
}

Result CableCheck::process_request([[maybe_unused]] const message_din::CableCheckRequest& req) {
    if (not cable_check_initiated) {
        m_ctx.feedback.signal(session::feedback::Signal::START_CABLE_CHECK);
        cable_check_initiated = true;
        // The EV is now in the DC charging phase and must stay in C/D; an unexpected CP State B from here
        // on is a fault ([V2G-DC-668], handled in the engine's CpStateChanged path).
        m_ctx.expect_cp_state_cd = true;
    }

    const auto res = handle_request(req, m_ctx.evse().cable_check_done, m_ctx.evse().cable_check_fault,
                                    m_ctx.get_session_id(), m_ctx.error_status_code(),
                                    m_ctx.evse().charger_stop_requested, m_ctx.reported_isolation_level());
    m_ctx.respond(res);

    if (res.response_code >= dt::ResponseCode::FAILED) {
        m_ctx.session_stopped = true;
        return {};
    }

    if (m_ctx.evse().cable_check_done) {
        return m_ctx.create_state<PreChargeStart>();
    }
    return {};
}

Result CableCheck::on_event(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control_data = m_ctx.get_control_event<d20::CableCheckFinished>()) {
            // Absence of the event means the check is still ongoing [V2G-DC-890].
            if (static_cast<bool>(*control_data)) {
                m_ctx.set_cable_check_done();
            } else {
                m_ctx.set_cable_check_fault();
            }
        }
        // Parked while waiting for CP State C/D ([V2G-DC-967]): resume as soon as it is reported.
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
        if (timeout and *timeout == d20::TimeoutType::CPSTATE and pending_req.has_value()) {
            // [V2G-DC-967]: no CP State C/D in time, so the cable check must not run. The FAILED response arms
            // the FailedTermination path (oscillator off + SECC-side TCP close).
            logf_warning("no CP State C/D within V2G_SECC_CPState_Detection_Timeout, CableCheck -> FAILED");
            pending_req.reset();
            message_din::CableCheckResponse res;
            setup_header(res.header, m_ctx.get_session_id());
            res.evse_processing = dt::EvseProcessing::Finished;
            res.dc_evse_status.evse_status_code = dt::DcEvseStatusCode::EVSE_Shutdown;
            res.dc_evse_status.evse_isolation_status = dt::IsolationLevel::Valid;
            m_ctx.respond(response_with_code(res, dt::ResponseCode::FAILED));
            m_ctx.session_stopped = true;
        }
        return {};
    }

    return {};
}

Result CableCheck::on_request(const message_din::Variant& received) {
    if (const auto req = received.get_if<message_din::CableCheckRequest>()) {

        m_ctx.report_ev_status(req->dc_ev_status);

        // [V2G-DC-967]: the SECC must detect CP State C (or D, [V2G-DC-493]) within
        // V2G_SECC_CPState_Detection_Timeout, so park the request until it is reported or the timeout fails it.
        const bool cp_charging_state =
            m_ctx.evse().current_cp_state == d20::CpState::C or m_ctx.evse().current_cp_state == d20::CpState::D;
        if (not cp_charging_state) {
            pending_req = *req;
            m_ctx.arm_cp_state_timeout(TIMEOUT_CPSTATE_DETECTION_MS);
            return {};
        }

        return process_request(*req);
    }

    // [V2G-DC-965]/[V2G-DC-499] admit a SessionStopReq here.
    if (const auto stop = received.get_if<message_din::SessionStopRequest>()) {
        return process_session_stop(m_ctx, *stop);
    }

    logf_warning("Expected CableCheckReq or SessionStopReq! But code type id: %d", received.get_type());
    respond_sequence_error(m_ctx, received);
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::state
