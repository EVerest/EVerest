// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <utility>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d2/context.hpp>
#include <iso15118/ev/d2/state/session_stop.hpp>
#include <iso15118/message_2/common_types.hpp>
#include <iso15118/message_2/variant.hpp>

namespace iso15118::ev::d2 {

// Validate a response: expected variant, session id of the active session, response code below FAILED.
// On failure logs, stops the session and returns nullptr; the caller returns Result::stopping().
template <typename Res> const Res* expect_response(Context& ctx, const message_2::Variant& variant) {
    const auto* res = variant.get_if<Res>();
    if (res == nullptr) {
        logf_error("Unexpected ISO 15118-2 response, got message type id: %d", static_cast<int>(variant.get_type()));
        ctx.stop_session();
        return nullptr;
    }
    if (res->header.session_id != ctx.get_session_id()) {
        logf_error("Response session_id does not match the active session");
        ctx.stop_session();
        return nullptr;
    }
    if (res->response_code >= message_2::datatypes::ResponseCode::FAILED) {
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

inline bool is_dc_mode(message_2::datatypes::EnergyTransferMode mode) {
    using message_2::datatypes::EnergyTransferMode;
    return mode == EnergyTransferMode::DC_core or mode == EnergyTransferMode::DC_extended or
           mode == EnergyTransferMode::DC_combo_core or mode == EnergyTransferMode::DC_unique;
}

} // namespace iso15118::ev::d2
