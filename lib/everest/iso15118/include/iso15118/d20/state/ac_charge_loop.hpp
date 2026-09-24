// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

#include <iso15118/d20/ac_powers.hpp>
#include <iso15118/d20/dynamic_mode_parameters.hpp>
#include <iso15118/d20/pause_notification.hpp>

namespace iso15118::d20::state {
struct AC_ChargeLoop : public StateBase {
    AC_ChargeLoop(Context& ctx) : StateBase(ctx, StateID::AC_ChargeLoop) {
    }

    void enter() final;

    Result feed(Event) final;

private:
    std::optional<float> target_frequency; // TODO(SL): Adding updating feature
    bool stop{false};
    bool pause{false};
    PauseNotification pause_notification;

    UpdateDynamicModeParameters dynamic_parameters{};
    AcTargetPower target_powers{};
    AcPresentPower present_powers{};

    bool first_entry_in_charge_loop{true};
};

} // namespace iso15118::d20::state
