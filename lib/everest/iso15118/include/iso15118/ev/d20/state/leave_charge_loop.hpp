// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/power_delivery.hpp>
#include <iso15118/ev/d20/states.hpp>
#include <iso15118/ev/session/stop_reason.hpp>
#include <iso15118/message/common_types.hpp>

namespace iso15118::ev::d20::state {

// At a charge loop response: the EVSE's Terminate or Pause and the EV's latched stop or pause
// combine into the one reason PowerDelivery(Stop) carries to SessionStop.
inline std::optional<Result> leave_charge_loop(Context& ctx,
                                               const std::optional<message_20::datatypes::EvseStatus>& status) {
    using message_20::datatypes::EvseNotification;
    std::optional<session::StopReason> evse;
    if (status.has_value()) {
        switch (status->notification) {
        case EvseNotification::Terminate:
            ctx.feedback.stop_from_charger();
            evse = session::StopReason::EvseTerminate;
            break;
        case EvseNotification::Pause:
            ctx.feedback.pause_from_charger();
            evse = session::StopReason::EvsePause;
            break;
        case EvseNotification::ExitStandby:
        case EvseNotification::ScheduleRenegotiation:
        case EvseNotification::ServiceRenegotiation:
        case EvseNotification::MeteringConfirmation:
            break;
        }
    }

    const auto ev = session::requested_by_ev(ctx);
    if (not ev.has_value() and not evse.has_value()) {
        return std::nullopt;
    }
    return Result{to_power_delivery(ctx, PowerDelivery::Stop{session::combine(ev, evse)})};
}

} // namespace iso15118::ev::d20::state
