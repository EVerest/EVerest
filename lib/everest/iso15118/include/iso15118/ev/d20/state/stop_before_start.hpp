// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <utility>

#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/session_stop.hpp>
#include <iso15118/ev/d20/states.hpp>

namespace iso15118::ev::d20::state {

// [V2G20-2644]: before PowerDeliveryReq(Start) is sent, an EV-side stop or pause goes straight to
// SessionStop, which carries the reason.
inline std::optional<Result> stop_before_start(Context& ctx) {
    if (ctx.is_stop_charging_requested() or ctx.is_pause_charging_requested()) {
        return Result{ctx.create_state<SessionStop>()};
    }
    return std::nullopt;
}

} // namespace iso15118::ev::d20::state
