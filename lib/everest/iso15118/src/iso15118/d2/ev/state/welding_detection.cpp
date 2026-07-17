// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/ev/state/welding_detection.hpp>

#include <iso15118/d2/ev/state/session_stop.hpp>
#include <iso15118/d2/ev/timeouts.hpp>

#include <iso15118/detail/d2/ev/state/state_helper.hpp>
#include <iso15118/detail/d2/ev/state/welding_detection.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::ev::state {

namespace welding_detection {

message_2::WeldingDetectionRequest create_request(const dt::DC_EVStatus& dc_ev_status) {
    message_2::WeldingDetectionRequest req;
    req.dc_ev_status = dc_ev_status;
    return req;
}

Result handle_response(const message_2::WeldingDetectionResponse& res) {
    Result result;
    result.valid = (res.response_code < dt::ResponseCode::FAILED);
    if (not result.valid) {
        return result;
    }
    result.present_voltage = static_cast<float>(dt::from_physical_value(res.evse_present_voltage));
    return result;
}

bool should_finish_welding(int cycles, float present_voltage) {
    return (present_voltage < WELDING_DETECTION_SAFE_VOLTAGE_V) or (cycles >= WELDING_DETECTION_CYCLES);
}

} // namespace welding_detection

using namespace welding_detection;

void WeldingDetection::enter() {
    m_ctx.log.enter_state("WeldingDetection");
}

void WeldingDetection::send(Event) {
    if (first_request) {
        m_ctx.start_timeout(d20::TimeoutType::ONGOING, TIMEOUT_WELDING_DETECTION_MS);
        first_request = false;
    }

    auto req = create_request(make_dc_ev_status(m_ctx, true));
    m_ctx.setup_header(req.header);
    m_ctx.send_request(req);
    m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_MS);
}

d2::ev::Result WeldingDetection::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        send(ev);
        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto* timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::ONGOING) {
            m_ctx.log("WeldingDetection ongoing timeout reached, terminating session");
            ongoing_timeout_reached = true;
        } else {
            m_ctx.log("WeldingDetection message timeout reached, terminating session");
        }
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_2::WeldingDetectionResponse>()) {
        const auto result = handle_response(*res);

        if (not result.valid) {
            m_ctx.log("WeldingDetection failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        ++cycles;
        if (should_finish_welding(cycles, result.present_voltage)) {
            m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
            return m_ctx.create_state<SessionStop>();
        }

        if (ongoing_timeout_reached) {
            m_ctx.session_stopped = true;
            return {};
        }

        send(ev);
        return {};
    }

    m_ctx.log("expected WeldingDetectionRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d2::ev::state
