// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

#include <cstdint>
#include <optional>

#include <iso15118/d20/dynamic_mode_parameters.hpp>
#include <iso15118/d20/pause_notification.hpp>
#include <iso15118/message/power_delivery.hpp>

namespace iso15118::d20::state {

struct DC_ChargeLoop : public StateBase {
    DC_ChargeLoop(Context& ctx) : StateBase(ctx, StateID::DC_ChargeLoop) {
    }

    void enter() final;

    Result feed(Event) final;

private:
    float present_voltage{0};
    float present_current{0};
    bool stop{false};
    bool pause{false};
    PauseNotification pause_notification;

    UpdateDynamicModeParameters dynamic_parameters;

    bool first_entry_in_charge_loop{true};

    // PowerDelivery(Stop) response held back until the power permissive is withdrawn
    std::optional<message_20::PowerDeliveryResponse> pending_stop_res;
    std::optional<bool> contactor_closed;

    Result send_pending_stop_res();
};

} // namespace iso15118::d20::state
