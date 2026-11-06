// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d2/state/welding_detection.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d2/state/session_stop.hpp>
#include <iso15118/ev/detail/d2/context_helper.hpp>
#include <iso15118/ev/detail/d2/state/state_helper.hpp>
#include <iso15118/ev/detail/d2/state/welding_detection.hpp>

namespace iso15118::ev::d2::state {

namespace welding_detection {

message_2::WeldingDetectionRequest create_request(const dt::DC_EVStatus& dc_ev_status) {
    message_2::WeldingDetectionRequest req;
    req.dc_ev_status = dc_ev_status;
    return req;
}

} // namespace welding_detection

void WeldingDetection::enter() {
    logf_debug("Enter state: WeldingDetection (ISO 15118-2)");
}

Result WeldingDetection::feed(Event ev) {
    if (ev == Event::RESPONSE_TIMEOUT) {
        return progress.response_timeout<Result>(m_ctx);
    }
    // A poll still queued is dropped and fails the check. One already sent is judged when its answer
    // arrives, against the bound this deadline shares.
    if (ev == Event::WELDING_TIMEOUT and m_ctx.has_request()) {
        m_ctx.discard_request();
        return to_session_stop(m_ctx, progress.report_failure(m_ctx));
    }
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();
    const auto* res = expect_response<message_2::WeldingDetectionResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    switch (progress.record(static_cast<float>(dt::from_physical_value(res->evse_present_voltage)),
                            m_ctx.welding_detection)) {
    case welding_detection::Outcome::Safe:
        return to_session_stop(m_ctx, m_stop_reason);
    case welding_detection::Outcome::Failed:
        return to_session_stop(m_ctx, progress.report_failure(m_ctx));
    case welding_detection::Outcome::Continue:
        break;
    }

    m_ctx.send_request(welding_detection::create_request(make_dc_ev_status(m_ctx.get_dc_params(), false)));
    return Result::awaiting();
}

} // namespace iso15118::ev::d2::state
