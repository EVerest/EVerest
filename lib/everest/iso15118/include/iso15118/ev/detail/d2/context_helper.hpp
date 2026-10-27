// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <utility>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d2/context.hpp>
#include <iso15118/ev/d2/state/session_stop.hpp>
#include <iso15118/ev/detail/context_helper.hpp>
#include <iso15118/ev/message_2/response_variant.hpp>
#include <iso15118/ev/session/stop_reason.hpp>
#include <iso15118/message_2/common_types.hpp>

namespace iso15118::ev::d2 {

// Validate a response: expected variant, session id of the active session, response code below FAILED.
// On failure, logs it unless the decoder already did, stops the session and returns nullptr; the
// caller then returns Result::stopping().
template <typename Res> const Res* expect_response(Context& ctx, const message_2::ResponseVariant& variant) {
    const auto* res = expect_response_type<Res>(ctx, variant);
    if (res == nullptr) {
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
    if (const auto reason = session::requested_by_ev(ctx)) {
        return Result{state::to_session_stop(ctx, *reason)};
    }
    return std::nullopt;
}

inline bool is_dc_mode(message_2::datatypes::EnergyTransferMode mode) {
    using message_2::datatypes::EnergyTransferMode;
    return mode == EnergyTransferMode::DC_core or mode == EnergyTransferMode::DC_extended or
           mode == EnergyTransferMode::DC_combo_core or mode == EnergyTransferMode::DC_unique;
}

} // namespace iso15118::ev::d2
