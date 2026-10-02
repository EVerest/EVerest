// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/dc_welding_detection.hpp>
#include <iso15118/ev/d20/state/session_stop.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/dc_welding_detection.hpp>

namespace iso15118::ev::d20::state {

namespace {

message_20::DC_WeldingDetectionRequest make_request(const SessionId& session,
                                                    message_20::datatypes::Processing processing) {
    message_20::DC_WeldingDetectionRequest req;
    setup_header(req.header, session);
    req.processing = processing;
    return req;
}

} // namespace

void DC_WeldingDetection::enter() {
    m_ctx.send_request(make_request(m_ctx.get_session(), message_20::datatypes::Processing::Ongoing));
}

Result DC_WeldingDetection::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::DC_WeldingDetectionResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    // While EVProcessing is Ongoing the SECC stays in welding detection; Finished is what lets it
    // advance to SessionStop. The closing request is sent here and its response ends the state,
    // the same shape DC_PreCharge uses.
    if (finished_sent) {
        return m_ctx.create_state<SessionStop>();
    }

    finished_sent = true;
    m_ctx.send_request(make_request(m_ctx.get_session(), message_20::datatypes::Processing::Finished));
    return Result::awaiting();
}

} // namespace iso15118::ev::d20::state
