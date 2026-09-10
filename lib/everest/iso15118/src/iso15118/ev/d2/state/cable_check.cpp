// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d2/state/cable_check.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d2/state/pre_charge.hpp>
#include <iso15118/ev/detail/d2/context_helper.hpp>
#include <iso15118/ev/detail/d2/state/cable_check.hpp>
#include <iso15118/ev/detail/d2/state/state_helper.hpp>

namespace iso15118::ev::d2::state {

namespace cable_check {

message_2::CableCheckRequest create_request(const dt::DC_EVStatus& dc_ev_status) {
    message_2::CableCheckRequest req;
    req.dc_ev_status = dc_ev_status;
    return req;
}

Result handle_response(const message_2::CableCheckResponse& res) {
    Result result;
    result.finished = (res.evse_processing == dt::EVSEProcessing::Finished);
    if (not result.finished) {
        return result;
    }
    result.evse_ready = (res.dc_evse_status.status_code == dt::DC_EVSEStatusCode::EVSE_Ready);
    // Valid and Warning are accepted (EvseV2G parity); Invalid/Fault/No_IMD/absent terminate.
    result.isolation_ok = res.dc_evse_status.isolation_status.has_value() and
                          (res.dc_evse_status.isolation_status.value() == dt::IsolationLevel::Valid or
                           res.dc_evse_status.isolation_status.value() == dt::IsolationLevel::Warning);
    return result;
}

} // namespace cable_check

void CableCheck::enter() {
    logf_debug("Enter state: CableCheck (ISO 15118-2)");
    // [V2G2-847]: the first CableCheckReq only goes out once the EV applied CP state C or D. Without
    // CP-state feedback there is nothing to wait for; the Session's ongoing guard bounds the wait.
    if (m_ctx.has_cp_state_feedback() and not m_ctx.cp_state_c_or_d()) {
        logf_debug("CableCheck holds the first request until CP state C or D");
        return;
    }
    m_ctx.send_request(cable_check::create_request(make_dc_ev_status(m_ctx.get_dc_params(), true)));
    request_sent = true;
}

Result CableCheck::feed(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (request_sent) {
            return Result::ignored();
        }
        // Still waiting for CP state C/D: a stop or pause ends the session without a CableCheckReq.
        if (auto stop = stop_before_start(m_ctx)) {
            return std::move(*stop);
        }
        if (not m_ctx.cp_state_c_or_d()) {
            return Result::ignored();
        }
        m_ctx.send_request(cable_check::create_request(make_dc_ev_status(m_ctx.get_dc_params(), true)));
        request_sent = true;
        return Result::awaiting();
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();
    const auto* res = expect_response<message_2::CableCheckResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    const auto result = cable_check::handle_response(*res);

    if (not result.finished) {
        m_ctx.send_request(cable_check::create_request(make_dc_ev_status(m_ctx.get_dc_params(), true)));
        return Result::awaiting();
    }

    if (not result.evse_ready or not result.isolation_ok) {
        logf_error("CableCheck finished but the EVSE is not ready or isolation is not valid; stopping the session");
        m_ctx.stop_session();
        return Result::stopping();
    }

    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }
    return m_ctx.create_state<PreCharge>();
}

} // namespace iso15118::ev::d2::state
