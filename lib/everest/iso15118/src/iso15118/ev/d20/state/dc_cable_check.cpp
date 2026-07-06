// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/dc_cable_check.hpp>
#include <iso15118/ev/d20/state/dc_pre_charge.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/dc_cable_check.hpp>

namespace iso15118::ev::d20::state {

namespace {

message_20::DC_CableCheckRequest make_request(const SessionId& session) {
    message_20::DC_CableCheckRequest req;
    setup_header(req.header, session);
    return req;
}

} // namespace

void DC_CableCheck::enter() {
    m_ctx.respond(make_request(m_ctx.get_session()));
}

Result DC_CableCheck::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::DC_CableCheckResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return {};
    }

    if (res->processing == message_20::datatypes::Processing::Finished) {
        return m_ctx.create_state<DC_PreCharge>();
    }

    // Processing::Ongoing: re-poll
    m_ctx.respond(make_request(m_ctx.get_session()));
    return {};
}

} // namespace iso15118::ev::d20::state
