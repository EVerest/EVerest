// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <utility>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/session_stop.hpp>
#include <iso15118/ev/d20/states.hpp>
#include <iso15118/ev/session/stop_reason.hpp>

namespace iso15118::ev::d20::state {

// [V2G20-2644]: before PowerDeliveryReq(Start) is sent, an EV-side stop or pause goes straight to
// SessionStop with ChargingSession=Terminate, so a pause ends the session as a stop.
inline std::optional<Result> stop_before_start(Context& ctx) {
    if (const auto reason = session::requested_by_ev(ctx)) {
        if (session::is_pause(*reason)) {
            logf_warning("pause requested before charging started; terminating per [V2G20-2644]");
        }
        return Result{to_session_stop(ctx, session::StopReason::EvStop)};
    }
    return std::nullopt;
}

} // namespace iso15118::ev::d20::state
