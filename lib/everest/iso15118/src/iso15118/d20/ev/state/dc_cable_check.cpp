// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/ev/state/dc_cable_check.hpp>

#include <iso15118/d20/ev/state/dc_pre_charge.hpp>
#include <iso15118/d20/ev/timeouts.hpp>

#include <iso15118/detail/d20/ev/state/dc_cable_check.hpp>
#include <iso15118/detail/d20/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d20::ev::state {

namespace dc_cable_check {

message_20::DC_CableCheckRequest create_request() {
    return message_20::DC_CableCheckRequest{};
}

Result handle_response(const message_20::DC_CableCheckResponse& res) {
    Result result;
    result.valid = (res.response_code < dt::ResponseCode::FAILED);
    result.finished = (res.processing == dt::Processing::Finished);
    return result;
}

} // namespace dc_cable_check

using namespace dc_cable_check;

void DC_CableCheck::enter() {
    m_ctx.log.enter_state("DC_CableCheck");
}

void DC_CableCheck::send(Event) {
    if (first_request) {
        // The ongoing timer also bounds the wait for CP state C/D below: if the state never comes the
        // session terminates via the regular cable check ongoing timeout.
        m_ctx.start_timeout(d20::TimeoutType::ONGOING, TIMEOUT_CABLE_CHECK_MS);
        first_request = false;

        // [V2G20-1418]: the EV shall apply CP state C or D before requesting the cable check. When the
        // module reports the applied CP state, hold the first DC_CableCheckReq until state C/D is seen.
        if (m_ctx.session_config.has_cp_state_feedback and not m_ctx.cp_state_c_or_d) {
            m_ctx.log("DC_CableCheck: waiting for CP state C/D before sending the first DC_CableCheckReq");
            waiting_for_cp_state = true;
        }
    }

    if (waiting_for_cp_state) {
        if (not m_ctx.cp_state_c_or_d) {
            return;
        }
        waiting_for_cp_state = false;
    }

    auto req = create_request();
    m_ctx.setup_header(req.header);
    m_ctx.send_request(req);
    m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_MS);
}

d20::ev::Result DC_CableCheck::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        send(ev);
        return {};
    }

    if (ev == Event::CONTROL_MESSAGE) {
        handle_stop_control_event(m_ctx);
        if (waiting_for_cp_state and m_ctx.cp_state_c_or_d) {
            send(ev);
        }
        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto* timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::ONGOING) {
            m_ctx.log("DC_CableCheck ongoing timeout reached, terminating session");
            ongoing_timeout_reached = true;
        } else {
            m_ctx.log("DC_CableCheck message timeout reached, terminating session");
        }
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_20::DC_CableCheckResponse>()) {
        const auto result = handle_response(*res);

        if (not result.valid) {
            m_ctx.log("DC_CableCheck failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        if (not result.finished) {
            if (ongoing_timeout_reached) {
                m_ctx.session_stopped = true;
                return {};
            }
            send(ev);
            return {};
        }

        m_ctx.stop_timeout(d20::TimeoutType::ONGOING);

        if (auto stop = stop_if_pending(m_ctx)) {
            return stop;
        }
        return m_ctx.create_state<DC_PreCharge>();
    }

    m_ctx.log("expected DC_CableCheckRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d20::ev::state
