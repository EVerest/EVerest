// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include "command_registry.hpp"
#include "simulation_command.hpp"
#include <generated/types/board_support_common.hpp>
#include <generated/types/iso15118.hpp>
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

// The iso_start_v2g_session the ISO15118_ev provider accepted, the power path its energy mode drives
// and the transfer mode it requested. A charger pause or stop restarts exactly this start. An accepted
// start holds the four arguments the command registry passed (energy mode, payment option, departure
// time, energy amount): the registry fills an omitted departure time and energy amount from config and
// an omitted payment option with the literal "auto". Before any accepted start the one argument "ac"
// stands in, so a restart from it takes those defaults again.
struct AcceptedV2gStart {
    std::vector<std::string> arguments{"ac"};
    EnergyMode energy_mode{EnergyMode::AC};
    types::iso15118::EnergyTransferMode transfer_mode{types::iso15118::EnergyTransferMode::AC_single_phase_core};

    std::string command() const {
        std::string command{"iso_start_v2g_session "};
        for (size_t i = 0; i < arguments.size(); ++i) {
            command += (i == 0 ? "" : ",") + arguments[i];
        }
        return command;
    }
};

struct SimulationData {

    SimulationData() = default;

    // Clears the per-command countdowns. Every tick-based command arms its counter only when it
    // finds none standing, so a command aborted mid-flight (its queue replaced by
    // modify/execute_charging_session) would otherwise bequeath its remaining ticks to the next
    // command that shares the counter: a wake sequence sent while the previous one parked on
    // 'sleep 36000' saw its 'iso_wait_for_stop 30' inherit the ~10 h countdown (bench-found
    // 2026-09-01). Vehicle state (plugged, CP, V2G) is deliberately NOT touched here.
    void clear_command_ticks() {
        sleep_ticks_left.reset();
        cp_c_pulse_ticks_left.reset();
        stop_hold_ticks_left.reset();
        stop_dwell_ticks_left.reset();
        pwm_wait_ticks_left.reset();
    }

    // Takes over the live V2G session of `from`: the matched link it runs on, the accepted start and
    // the provider's feedback on it, which only the session-finished feedback or an unplug clears.
    void keep_v2g_session(const SimulationData& from) {
        slac_state = from.slac_state;
        v2g_session_active = from.v2g_session_active;
        v2g_start = from.v2g_start;
        iso_pwr_ready = from.iso_pwr_ready;
        iso_stopped = from.iso_stopped;
        power_delivery_stopped = from.power_delivery_stopped;
        iso_charger_paused = from.iso_charger_paused;
        dc_power_on = from.dc_power_on;
    }

    SimState state{SimState::UNPLUGGED};
    SimState last_state{SimState::UNDEFINED};
    types::slac::State slac_state{types::slac::State::UNMATCHED};
    std::optional<size_t> sleep_ticks_left{};
    std::optional<size_t> cp_c_pulse_ticks_left{};
    std::optional<size_t> stop_hold_ticks_left{};
    // Set once that hold releases: the pilot stays in B for this countdown before the stop completes.
    std::optional<size_t> stop_dwell_ticks_left{};
    // Countdown of the optional fallback timeout of iso_wait_pwm_is_running.
    std::optional<size_t> pwm_wait_ticks_left{};

    bool v2g_session_active{false};
    bool iso_stopped{false};
    bool power_delivery_stopped{false};
    bool iso_charger_paused{false};
    size_t evse_maxcurrent{0};
    size_t max_current{0};
    std::string payment{"ExternalPayment"};

    AcceptedV2gStart v2g_start{};
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
    bool full_soc_pause_requested{false};

    types::board_support_common::Event actual_bsp_event{types::board_support_common::Event::Disconnected};
    // Last CP state reported by the "waiting for plug-in" log, so that wait
    // logs once per distinct state instead of on every simulation tick.
    types::board_support_common::Event last_logged_wait_event{types::board_support_common::Event::Disconnected};
};
