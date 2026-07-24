// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include "command_registry.hpp"
#include "simulation_command.hpp"
#include <generated/types/board_support_common.hpp>
#include <generated/types/slac.hpp>
#include <optional>
#include <queue>
#include <string>
#include <utility>
#include <vector>

enum class SimState {
    UNPLUGGED,
    PLUGGED_IN,
    CHARGING_REGULATED,
    CHARGING_FIXED,
    ERROR_E,
    DIODE_FAIL,
    ISO_POWER_READY,
    ISO_CHARGING_REGULATED,
    BCB_TOGGLE,
    UNDEFINED,
};

enum class EnergyMode {
    AC,
    DC,
};

enum class ResetBehavior {
    Full,
    KeepPlugState,
};

struct SimulationData {

    SimulationData() = default;

    SimState state{SimState::UNPLUGGED};
    SimState last_state{SimState::UNDEFINED};
    types::slac::State slac_state{types::slac::State::UNMATCHED};
    std::optional<size_t> sleep_ticks_left{};

    bool v2g_finished{false};
    bool v2g_session_active{false};
    bool iso_stopped{false};
    bool iso_charger_paused{false};
    size_t evse_maxcurrent{0};
    size_t max_current{0};
    std::string payment{"ExternalPayment"};

    EnergyMode energy_mode{EnergyMode::AC};
    std::optional<std::string> modify_charging_session_cmds{std::nullopt};

    bool iso_pwr_ready{false};

    size_t bcb_toggles{0};
    bool bcb_toggle_C{true};

    types::board_support_common::Ampacity pp;
    float rcd_current_ma{0.0f};
    float pwm_duty_cycle{0.0f};
    float last_pwm_duty_cycle{0.0f};

    bool dc_power_on{false};

    double battery_charge_wh{0};
    double battery_capacity_wh{0};

    types::board_support_common::Event actual_bsp_event{types::board_support_common::Event::Disconnected};

    // Reset per-session simulation data to defaults. With ResetBehavior::KeepPlugState keep the
    // CP/plug state (state + last_state) so it stays consistent with the frozen physical CP line
    // after a simulation queue completes instead of silently reverting to UNPLUGGED.
    // The V2G/SLAC session flags are always cleared: this reset only runs once the command queue
    // has finished and execution is inactive, so the logical session is over even though the
    // physical CP line stays frozen. Preserving v2g_session_active without a matching future
    // v2g_finished event would wedge iso_wait_v2g_session_stopped forever, and slac_state is
    // re-driven continuously by the real SLAC state subscription, so both are safe to drop.
    void reset(ResetBehavior behavior) {
        const auto saved_state = state;
        const auto saved_last_state = last_state;
        *this = SimulationData();
        if (behavior == ResetBehavior::KeepPlugState) {
            state = saved_state;
            last_state = saved_last_state;
        }
    }

    // Advance the "wait for V2G session stopped" step. Returns true when the command should
    // complete: either the active V2G session has now stopped, or no V2G session is active
    // (IEC session) so waiting would block the queue forever.
    bool advance_wait_v2g_session_stopped() {
        const bool session_stopped = v2g_session_active and v2g_finished;
        if (session_stopped) {
            v2g_finished = false;
            v2g_session_active = false;
        }
        return session_stopped or not v2g_session_active;
    }
};
