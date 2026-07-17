// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/ev/state/cable_check.hpp>

#include <iso15118/d2/ev/state/pre_charge.hpp>
#include <iso15118/d2/ev/timeouts.hpp>

#include <iso15118/detail/d2/ev/state/cable_check.hpp>
#include <iso15118/detail/d2/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::ev::state {

namespace cable_check {

message_2::CableCheckRequest create_request(const dt::DC_EVStatus& dc_ev_status) {
    message_2::CableCheckRequest req;
    req.dc_ev_status = dc_ev_status;
    return req;
}

Result handle_response(const message_2::CableCheckResponse& res) {
    if (res.response_code >= dt::ResponseCode::FAILED) {
        return {Action::Failed};
    }
    if (res.evse_processing != dt::EVSEProcessing::Finished) {
        return {Action::Retry};
    }

    Result result;
    result.action = Action::Done;
    result.evse_ready = (res.dc_evse_status.status_code == dt::DC_EVSEStatusCode::EVSE_Ready);
    // Accept Valid and Warning isolation levels (EvseV2G parity); Invalid/Fault/No_IMD/absent terminate.
    result.isolation_ok = res.dc_evse_status.isolation_status.has_value() and
                          (res.dc_evse_status.isolation_status.value() == dt::IsolationLevel::Valid or
                           res.dc_evse_status.isolation_status.value() == dt::IsolationLevel::Warning);
    return result;
}

} // namespace cable_check

using namespace cable_check;

void CableCheck::enter() {
    m_ctx.log.enter_state("CableCheck");
}

void CableCheck::send(Event) {
    if (first_request) {
        m_ctx.start_timeout(d20::TimeoutType::ONGOING, TIMEOUT_CABLE_CHECK_MS);
        first_request = false;
    }

    auto req = create_request(make_dc_ev_status(m_ctx, true));
    m_ctx.setup_header(req.header);
    m_ctx.send_request(req);
    m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_MS);
}

d2::ev::Result CableCheck::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        send(ev);
        return {};
    }

    if (ev == Event::CONTROL_MESSAGE) {
        handle_stop_control_event(m_ctx);
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

    if (const auto res = variant->get_if<message_2::CableCheckResponse>()) {
        const auto result = handle_response(*res);

        switch (result.action) {
        case Action::Failed:
            m_ctx.log("CableCheck failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        case Action::Done:
            m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
            if (not result.evse_ready or not result.isolation_ok) {
                m_ctx.log("CableCheck finished but EVSE not ready or isolation not valid, terminating session");
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

} // namespace iso15118::d2::ev::state
