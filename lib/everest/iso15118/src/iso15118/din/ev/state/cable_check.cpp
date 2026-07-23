// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/ev/state/cable_check.hpp>

#include <iso15118/din/ev/state/pre_charge.hpp>
#include <iso15118/din/ev/timeouts.hpp>

#include <iso15118/detail/din/ev/state/cable_check.hpp>
#include <iso15118/detail/din/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::ev::state {

namespace cable_check {

message_din::CableCheckRequest create_request(const dt::DcEvStatus& dc_ev_status) {
    message_din::CableCheckRequest req;
    req.dc_ev_status = dc_ev_status;
    return req;
}

Result handle_response(const message_din::CableCheckResponse& res) {
    if (res.response_code >= dt::ResponseCode::FAILED) {
        return {Action::Failed};
    }
    if (res.evse_processing != dt::EvseProcessing::Finished) {
        return {Action::Retry};
    }

    Result result;
    result.action = Action::Done;
    // [V2G-DC-893]: on Finished, disregard every EVSEStatusCode except EVSE_Shutdown / EVSE_Emergency-
    // Shutdown; [V2G-DC-894]: disregard EVSEIsolationStatus entirely. So only a shutdown code stops us.
    const auto status = res.dc_evse_status.evse_status_code;
    result.evse_shutdown =
        (status == dt::DcEvseStatusCode::EVSE_Shutdown or status == dt::DcEvseStatusCode::EVSE_EmergencyShutdown);
    return result;
}

} // namespace cable_check

using namespace cable_check;

void CableCheck::enter() {
    m_ctx.log.enter_state("CableCheck");
}

void CableCheck::send(Event) {
    if (first_request) {
        // The ongoing timer also bounds the wait for CP state C/D below: if the state never comes the
        // session terminates via the regular cable check ongoing timeout.
        m_ctx.start_timeout(d20::TimeoutType::ONGOING, TIMEOUT_CABLE_CHECK_MS);
        first_request = false;

        // [V2G-DC-547]: the EV shall apply CP state C or D before requesting the cable check. When the
        // module reports the applied CP state, hold the first CableCheckReq until state C/D is seen.
        if (m_ctx.session_config.has_cp_state_feedback and not m_ctx.cp_state_c_or_d) {
            m_ctx.log("CableCheck: waiting for CP state C/D before sending the first CableCheckReq");
            waiting_for_cp_state = true;
        }
    }

    if (waiting_for_cp_state) {
        if (not m_ctx.cp_state_c_or_d) {
            return;
        }
        waiting_for_cp_state = false;
    }

    auto req = create_request(make_dc_ev_status(m_ctx, true));
    m_ctx.setup_header(req.header);
    m_ctx.send_request(req);
    m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_MS);
}

din::ev::Result CableCheck::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        send(ev);
        return {};
    }

    if (ev == Event::CONTROL_MESSAGE) {
        handle_dc_control_event(m_ctx);
        handle_stop_control_event(m_ctx);
        if (waiting_for_cp_state and m_ctx.cp_state_c_or_d) {
            send(ev);
        }
        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto* timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::ONGOING) {
            m_ctx.log("CableCheck ongoing timeout reached, terminating session");
            ongoing_timeout_reached = true;
        } else {
            m_ctx.log("CableCheck message timeout reached, terminating session");
        }
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_din::CableCheckResponse>()) {
        const auto result = handle_response(*res);

        switch (result.action) {
        case Action::Failed:
            m_ctx.log("CableCheck failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        case Action::Done:
            m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
            if (result.evse_shutdown) {
                m_ctx.log("CableCheck finished but EVSE signalled (emergency) shutdown, terminating session");
                m_ctx.session_stopped = true;
                return {};
            }
            if (auto stop = stop_if_pending(m_ctx)) {
                return stop;
            }
            return m_ctx.create_state<PreCharge>();
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

    m_ctx.log("expected CableCheckRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::ev::state
