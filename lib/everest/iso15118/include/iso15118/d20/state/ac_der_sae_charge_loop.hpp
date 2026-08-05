// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

#include <cstdint>
#include <optional>

#include <iso15118/d20/ac_powers.hpp>
#include <iso15118/d20/dynamic_mode_parameters.hpp>

namespace iso15118::d20::state {

struct SaeChargeLoopLogState {
    bool target_active_power_function_reported{false};
    std::optional<std::uint32_t> reported_der_alarm_status;
    bool reported_enabled_modes_mismatch{false};
};

struct AC_DER_SAE_ChargeLoop : public StateBase {
    AC_DER_SAE_ChargeLoop(Context& ctx) : StateBase(ctx, StateID::AC_DER_SAE_ChargeLoop) {
    }

    void enter() final;

    Result feed(Event) final;

private:
    bool stop{false};
    bool pause{false};

    UpdateDynamicModeParameters dynamic_parameters{};
    AcTargetPower target_powers{};
    AcPresentPower present_powers{};

    SaeChargeLoopLogState log_state{};

    bool first_entry_in_charge_loop{true};
};

} // namespace iso15118::d20::state
