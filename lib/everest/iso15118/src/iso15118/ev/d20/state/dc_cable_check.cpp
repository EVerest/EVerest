// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/dc_cable_check.hpp>
#include <iso15118/ev/d20/state/dc_pre_charge.hpp>
#include <iso15118/ev/d20/state/stop_before_start.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/dc_cable_check.hpp>

namespace iso15118::ev::d20::state {

message_20::DC_CableCheckRequest DC_CableCheck::make_request() {
    message_20::DC_CableCheckRequest req;
    return req;
}

Result DC_CableCheck::begin(Context& ctx) {
    // Without CP-state feedback there is nothing to wait for.
    if (ctx.has_cp_state_feedback() and not ctx.cp_state_c_or_d()) {
        return Result::waiting(ctx.create_state<DC_CableCheck>(Entry{false}));
    }
    ctx.send_request(make_request());
    return ctx.create_state<DC_CableCheck>(Entry{true});
}

void DC_CableCheck::enter() {
    // The session's ongoing guard bounds the wait for CP state C or D.
    if (not request_sent) {
        logf_debug("DC_CableCheck holds the first request until CP state C or D");
    }
}

Result DC_CableCheck::feed(Event ev) {
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
        m_ctx.send_request(make_request());
        request_sent = true;
        return Result::awaiting();
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    if (not request_sent) {
        logf_error("DC_CableCheck received a response before sending its request; stopping the session");
        m_ctx.stop_session();
        return Result::stopping();
    }

    const auto* res = expect_response<message_20::DC_CableCheckResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }

    if (res->processing == message_20::datatypes::Processing::Finished) {
        m_ctx.send_request(
            DC_PreCharge::make_request(message_20::datatypes::Processing::Ongoing, m_ctx.get_dc_params()));
        return m_ctx.create_state<DC_PreCharge>();
    }

    // Processing::Ongoing: re-poll
    m_ctx.send_request(make_request());
    return Result::awaiting();
}

} // namespace iso15118::ev::d20::state
