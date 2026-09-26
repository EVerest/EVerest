// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <utility>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/din/context.hpp>
#include <iso15118/ev/din/state/session_stop.hpp>
#include <iso15118/message_din/common_types.hpp>
#include <iso15118/message_din/variant.hpp>

namespace iso15118::ev::din {

// Validate a response: expected variant, session id of the active session, response code below FAILED.
// On failure logs, stops the session and returns nullptr; the caller returns Result::stopping().
template <typename Res> const Res* expect_response(Context& ctx, const message_din::Variant& variant) {
    const auto* res = variant.get_if<Res>();
    if (res == nullptr) {
        logf_error("Unexpected DIN 70121 response, got message type id: %d", static_cast<int>(variant.get_type()));
        ctx.stop_session();
        return nullptr;
    }
    if (res->header.session_id != ctx.get_session_id()) {
        logf_error("Response session_id does not match the active session");
        ctx.stop_session();
        return nullptr;
    }
    if (res->response_code >= message_din::datatypes::ResponseCode::FAILED) {
        logf_error("Response rejected with response_code: %d", static_cast<int>(res->response_code));
        ctx.stop_session();
        return nullptr;
    }
    return res;
}

// Before PowerDelivery(Start): an EV stop or pause goes straight to SessionStop.
inline std::optional<Result> stop_before_start(Context& ctx) {
    if (ctx.is_stop_charging_requested() or ctx.is_pause_charging_requested()) {
        return Result{ctx.create_state<state::SessionStop>()};
    }
    return std::nullopt;
}

} // namespace iso15118::ev::din
