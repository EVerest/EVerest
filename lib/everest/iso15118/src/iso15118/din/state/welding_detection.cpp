// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/din/state/welding_detection.hpp>

#include <iso15118/din/state/session_stop.hpp>

#include <iso15118/detail/din/state/constants.hpp>
#include <iso15118/detail/din/state/sequence_error.hpp>
#include <iso15118/detail/din/state/session_stop.hpp>
#include <iso15118/detail/din/state/state_helper.hpp>
#include <iso15118/detail/din/state/welding_detection.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::state {

message_din::WeldingDetectionResponse handle_request([[maybe_unused]] const message_din::WeldingDetectionRequest& req,
                                                     float present_voltage, const dt::SessionId& session_id,
                                                     std::optional<dt::DcEvseStatusCode> error_status_code) {
    message_din::WeldingDetectionResponse res;
    setup_header(res.header, session_id);

    // Mandatory in WeldingDetectionRes even on a FAILED_UnknownSession response, so populate them first.
    // Welding detection follows PowerDelivery(ReadyToChargeState false), so side B is disabled: EVSE_Shutdown
    // (IEC 61851-23:2023 Table CC.9 t104, CC.7.6.17 step 3), and a module fault overrides it.
    res.dc_evse_status.evse_status_code = error_status_code.value_or(dt::DcEvseStatusCode::EVSE_Shutdown);
    res.dc_evse_status.evse_isolation_status = dt::IsolationLevel::Valid;
    res.evse_present_voltage = present_voltage;
    return response_with_code(res, dt::ResponseCode::OK);
}

void WeldingDetection::enter() {
    logf_debug("Enter state: WeldingDetection");
}

void WeldingDetection::process_request([[maybe_unused]] const message_din::WeldingDetectionRequest& req) {
    auto res = handle_request(req, m_ctx.evse().present_voltage, m_ctx.get_session_id(), m_ctx.error_status_code());
    apply_isolation_status(m_ctx, res.dc_evse_status);
    m_ctx.respond(res);

    if (res.response_code >= dt::ResponseCode::FAILED) {
        m_ctx.session_stopped = true;
    }
}

// Answered once CP State B is measured and values of the welding detection voltage source measured after
// the request arrived, or waiting for them expired [IEC 61851-23:2023 CC.6.3, Table CC.9 t105 to t106].
void WeldingDetection::answer_pending_req() {
    if (not pending_req.has_value()) {
        return;
    }
    // A session that ended meanwhile (CP State A, error) gets no answer to a parked request.
    if (m_ctx.session_stopped) {
        pending_req.reset();
        m_ctx.clear_cp_state_timeout();
        m_ctx.clear_welding_voltage_timeout();
        return;
    }
    if (m_ctx.power_delivery_stopped and m_ctx.evse().current_cp_state != d20::CpState::B) {
        return;
    }
    if (not m_ctx.welding_detection.has_fresh_voltage() and not welding_voltage_wait_expired) {
        return;
    }

    m_ctx.clear_cp_state_timeout();
    m_ctx.clear_welding_voltage_timeout();
    const auto req = pending_req.value();
    pending_req.reset();
    process_request(req);
}

// With every update outstanding the source stalled; with one in, the value is at worst one poll interval
// older than the request.
void WeldingDetection::log_welding_voltage_timeout() const {
    if (m_ctx.welding_detection.updates_since_request() == 0) {
        logf_warning("No value from the welding detection voltage source within %d ms of the WeldingDetectionReq, "
                     "answering with the last present voltage",
                     d20::TIMEOUT_WELDING_VOLTAGE);
    } else {
        logf_info("Only one value from the welding detection voltage source within %d ms of the "
                  "WeldingDetectionReq, answering with it",
                  d20::TIMEOUT_WELDING_VOLTAGE);
    }
}

Result WeldingDetection::on_event(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control_data = m_ctx.get_control_event<d20::PresentVoltageCurrent>()) {
            m_ctx.set_present_voltage(control_data->voltage);
            m_ctx.welding_detection.observe(*control_data);
        }
        // Parked while waiting for CP State B ([V2G-DC-988]) or the welding detection voltage: resume as
        // soon as both are there.
        if (pending_req.has_value()) {
            answer_pending_req();
            return {};
        }
        if (m_ctx.evse().current_cp_state == d20::CpState::B) {
            if (pending_stop.has_value()) {
                const auto req = pending_stop.value();
                pending_stop.reset();
                return process_session_stop(m_ctx, req);
            }
        }
        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto* timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::ONGOING) {
            logf_warning("WeldingDetection timeout reached, terminating session");
            m_ctx.session_stopped = true;
        } else if (timeout and *timeout == d20::TimeoutType::WELDING_VOLTAGE and pending_req.has_value()) {
            log_welding_voltage_timeout();
            welding_voltage_wait_expired = true;
            answer_pending_req();
        } else if (timeout and *timeout == d20::TimeoutType::CPSTATE and pending_stop.has_value()) {
            // [V2G-DC-556]: same window, but a SessionStopReq was parked -- answer its own type with FAILED.
            logf_warning("no CP State B within V2G_SECC_CPState_Detection_Timeout, SessionStop -> FAILED");
            pending_stop.reset();
            message_din::SessionStopResponse stop_res;
            setup_header(stop_res.header, m_ctx.get_session_id());
            m_ctx.respond(response_with_code(stop_res, dt::ResponseCode::FAILED));
            m_ctx.session_stopped = true;
        } else if (timeout and *timeout == d20::TimeoutType::CPSTATE and pending_req.has_value()) {
            // [V2G-DC-556]: no CP State B in time. The FAILED response arms the FailedTermination path
            // (oscillator off without delay + SECC-side TCP close).
            logf_warning("no CP State B within V2G_SECC_CPState_Detection_Timeout, WeldingDetection -> FAILED");
            pending_req.reset();
            m_ctx.clear_welding_voltage_timeout();
            message_din::WeldingDetectionResponse res;
            setup_header(res.header, m_ctx.get_session_id());
            res.dc_evse_status.evse_status_code =
                m_ctx.error_status_code().value_or(dt::DcEvseStatusCode::EVSE_Shutdown);
            res.dc_evse_status.evse_isolation_status = dt::IsolationLevel::Valid;
            apply_isolation_status(m_ctx, res.dc_evse_status);
            res.evse_present_voltage = m_ctx.evse().present_voltage;
            m_ctx.respond(response_with_code(res, dt::ResponseCode::FAILED));
            m_ctx.session_stopped = true;
        }
        return {};
    }

    return {};
}

Result WeldingDetection::on_request(const message_din::Variant& received) {
    if (const auto req = received.get_if<message_din::WeldingDetectionRequest>()) {

        m_ctx.report_ev_status(req->dc_ev_status);

        if (not welding_started) {
            m_ctx.start_timeout(d20::TimeoutType::ONGOING, TIMEOUT_WELDING_DETECTION_MS);
            welding_started = true;
        }

        pending_req = *req;
        m_ctx.welding_detection.request_received();
        welding_voltage_wait_expired = false;
        m_ctx.arm_welding_voltage_timeout();
        // [V2G-DC-988]: give the EV V2G_SECC_CPState_Detection_Timeout from the request before failing.
        if (m_ctx.power_delivery_stopped and m_ctx.evse().current_cp_state != d20::CpState::B) {
            m_ctx.arm_cp_state_timeout(TIMEOUT_CPSTATE_DETECTION_MS);
        }

        answer_pending_req();
        return {};
    }

    // [V2G-DC-459]/[V2G-DC-469] admit a SessionStopReq here too, and it takes the same CP State B gate:
    // [V2G-DC-988] applies to the request following PowerDelivery(off) whichever of the two it is.
    if (const auto stop = received.get_if<message_din::SessionStopRequest>()) {
        if (m_ctx.power_delivery_stopped and m_ctx.evse().current_cp_state != d20::CpState::B) {
            pending_stop = *stop;
            m_ctx.arm_cp_state_timeout(TIMEOUT_CPSTATE_DETECTION_MS);
            return {};
        }
        return process_session_stop(m_ctx, *stop);
    }

    logf_warning("Expected WeldingDetectionReq or SessionStopReq! But code type id: %d", received.get_type());
    respond_sequence_error(m_ctx, received);
    return {};
}

} // namespace iso15118::din::state
