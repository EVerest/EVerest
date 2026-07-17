// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/ev/state/dc_welding_detection.hpp>

#include <iso15118/d20/ev/state/session_stop.hpp>
#include <iso15118/d20/ev/timeouts.hpp>

#include <iso15118/detail/d20/ev/state/dc_welding_detection.hpp>
#include <iso15118/detail/d20/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d20::ev::state {

namespace dc_welding_detection {

message_20::DC_WeldingDetectionRequest create_request(dt::Processing processing) {
    message_20::DC_WeldingDetectionRequest req;
    req.processing = processing;
    return req;
}

Result handle_response(const message_20::DC_WeldingDetectionResponse& res) {
    return {res.response_code < dt::ResponseCode::FAILED};
}

} // namespace dc_welding_detection

using namespace dc_welding_detection;

void DC_WeldingDetection::enter() {
    m_ctx.log.enter_state("DC_WeldingDetection");
}

void DC_WeldingDetection::send(Event) {
    if (first_request) {
        m_ctx.start_timeout(d20::TimeoutType::ONGOING, TIMEOUT_WELDING_DETECTION_MS);
        first_request = false;
    }

    const auto processing = finish ? dt::Processing::Finished : dt::Processing::Ongoing;
    if (processing == dt::Processing::Finished) {
        sent_finished = true;
    }

    auto req = create_request(processing);
    m_ctx.setup_header(req.header);
    m_ctx.send_request(req);
    m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_MS);
}

d20::ev::Result DC_WeldingDetection::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        send(ev);
        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto* timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::ONGOING) {
            m_ctx.log("DC_WeldingDetection ongoing timeout reached, terminating session");
            ongoing_timeout_reached = true;
        } else {
            m_ctx.log("DC_WeldingDetection message timeout reached, terminating session");
        }
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_20::DC_WeldingDetectionResponse>()) {
        const auto result = handle_response(*res);

        if (not result.valid) {
            m_ctx.log("DC_WeldingDetection failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        if (sent_finished) {
            m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
            return m_ctx.create_state<SessionStop>();
        }

        ++cycles;
        if (cycles >= WELDING_DETECTION_CYCLES) {
            finish = true;
        }

        if (ongoing_timeout_reached) {
            m_ctx.session_stopped = true;
            return {};
        }

        send(ev);
        return {};
    }

    m_ctx.log("expected DC_WeldingDetectionRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d20::ev::state
