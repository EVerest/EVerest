// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/dc_welding_detection.hpp>
#include <iso15118/ev/d20/state/session_stop.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/ev/welding_detection.hpp>
#include <iso15118/message/dc_welding_detection.hpp>

namespace iso15118::ev::d20::state {

message_20::DC_WeldingDetectionRequest DC_WeldingDetection::make_request(message_20::datatypes::Processing processing) {
    message_20::DC_WeldingDetectionRequest req;
    req.processing = processing;
    return req;
}

Result DC_WeldingDetection::feed(Event ev) {
    if (ev == Event::RESPONSE_TIMEOUT) {
        return progress.response_timeout<Result>(m_ctx);
    }
    // A poll still queued is dropped and fails the check. One already sent is judged when its answer
    // arrives, against the bound this deadline shares.
    if (ev == Event::WELDING_TIMEOUT and not progress.concluded() and m_ctx.has_request()) {
        m_ctx.discard_request();
        m_stop_reason = progress.report_failure(m_ctx);
        return finish();
    }
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::DC_WeldingDetectionResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    // While EVProcessing is Ongoing the SECC stays in welding detection; Finished is what lets it
    // advance to SessionStop. finish() sends the closing request once the check concludes, and its
    // response ends the state, the same shape DC_PreCharge uses.
    if (progress.concluded()) {
        return to_session_stop(m_ctx, m_stop_reason);
    }

    const auto outcome =
        progress.record(message_20::datatypes::from_RationalNumber(res->present_voltage), m_ctx.welding_detection);
    switch (outcome) {
    case welding_detection::Outcome::Safe:
        break;
    case welding_detection::Outcome::Failed:
        m_stop_reason = progress.report_failure(m_ctx);
        break;
    case welding_detection::Outcome::Continue:
        m_ctx.send_request(make_request(message_20::datatypes::Processing::Ongoing));
        return Result::awaiting();
    }
    return finish();
}

Result DC_WeldingDetection::finish() {
    m_ctx.send_request(make_request(message_20::datatypes::Processing::Finished));
    return Result::awaiting();
}

} // namespace iso15118::ev::d20::state
