// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include "simulation_data.hpp"

#include "../EvManager.hpp"
#include <generated/interfaces/ISO15118_ev/Interface.hpp>
#include <generated/interfaces/ev_board_support/Interface.hpp>
#include <generated/interfaces/ev_manager/Implementation.hpp>
#include <generated/interfaces/ev_slac/Interface.hpp>
#include <generated/types/ev_board_support.hpp>

using CmdArguments = std::vector<std::string>;

class CarSimulation {
public:
    CarSimulation(const std::unique_ptr<ev_board_supportIntf>& r_ev_board_support_,
                  const std::vector<std::unique_ptr<ISO15118_evIntf>>& r_ev_,
                  const std::vector<std::unique_ptr<ev_slacIntf>>& r_slac_,
                  const std::unique_ptr<ev_managerImplBase>& p_ev_manager_, const module::Conf& config_) :
        r_ev_board_support(r_ev_board_support_),
        r_ev(r_ev_),
        r_slac(r_slac_),
        p_ev_manager(p_ev_manager_),
        config(config_),
        timepoint_last_update(std::chrono::steady_clock::now()){};
    ~CarSimulation() = default;

    // Forget the session (command countdowns, V2G flags, SLAC bookkeeping, SoC) but not the
    // vehicle's presence on the wire.
    //
    // A vehicle that is plugged in stays PLUGGED_IN, with nothing pending: the cable is still
    // mated, so "reset" must not turn into an unplug. Staging UNPLUGGED here and letting the
    // state machine apply it at the first tick of the NEXT command list is what made every
    // execute_charging_session (and every finished queue) yank the pilot to A -- on an MCS bench
    // the EV then vanished for the whole wake pulse and the EVSE saw a replug instead of the
    // CC.5.2.4 wake (bench-found 2026-09-01, TEST_PLAN findings 7 and 9). Only a pilot that
    // is genuinely gone resets the vehicle: unplug_vehicle() below, or the 'unplug' command.
    //
    // The control pilot measurement is the outside world, not simulation state: it survives the
    // reset either way. Dropping it back to Disconnected would make wait_for_real_plugin -- which
    // is level-triggered on purpose, so a vehicle that comes up on an already-energized pilot
    // still starts a session -- wait for an edge that has already happened and never comes again.
    void reset() {
        const auto measured_cp_state = sim_data.actual_bsp_event;
        const auto was_plugged = sim_data.state != SimState::UNPLUGGED;
        // A cp_c_pulse cut short by the reset would leave the readiness claim (CP C) standing on
        // the wire. Release it; nothing above PLUGGED_IN ever runs a pulse, so this is never a
        // C-exit out of an energized session.
        const auto pulse_in_flight = sim_data.cp_c_pulse_ticks_left.has_value();
        sim_data = SimulationData();
        sim_data.actual_bsp_event = measured_cp_state;
        sim_data.last_logged_wait_event = measured_cp_state;
        sim_data.battery_capacity_wh = config.dc_energy_capacity;
        double soc = config.soc;
        sim_data.battery_charge_wh = config.dc_energy_capacity * (soc / 100.0);
        if (was_plugged) {
            sim_data.state = SimState::PLUGGED_IN;
            sim_data.last_state = SimState::PLUGGED_IN;
            if (pulse_in_flight) {
                r_ev_board_support->call_set_cp_state(types::ev_board_support::EvCpState::B);
            }
        }
    }

    // The vehicle lost its pilot (plug-out, or the EVSE signalling E/F): forget the session AND
    // the presence. Leaves UNPLUGGED pending so the caller's next state_machine() tick runs the
    // unplug branch (CP A, power off, matching stopped, charging stopped).
    void unplug_vehicle() {
        sim_data.state = SimState::UNPLUGGED;
        reset();
    }

    // True while the vehicle is at most presenting itself (A or B on the pilot): no readiness
    // claim, no power, no toggle in flight. The states a new command list may safely replace.
    bool is_idle_on_the_wire() const {
        return sim_data.state == SimState::UNPLUGGED or sim_data.state == SimState::PLUGGED_IN;
    }

    void set_soc(double soc) {
        if (soc < 0 || soc > 100) {
            throw std::out_of_range("SoC value " + std::to_string(soc) + " is out of range (0-100)");
        }
        sim_data.battery_charge_wh = config.dc_energy_capacity * (soc / 100.0);
    }

    const SimState& get_state() const {
        return sim_data.state;
    }

    // See SimulationData::clear_command_ticks(): called whenever a new command list replaces the
    // queue, so no aborted command's countdown leaks into the new list.
    void clear_command_ticks() {
        sim_data.clear_command_ticks();
    }

    std::optional<std::string>& get_modify_charging_session_cmds() {
        return sim_data.modify_charging_session_cmds;
    }

    void update_modify_charging_session_cmds(const std::string& cmds) {
        sim_data.modify_charging_session_cmds.emplace(cmds);
    }

    void set_state(SimState state) {
        sim_data.state = state;
    }

    types::board_support_common::Event get_bsp_event() const {
        return sim_data.actual_bsp_event;
    }

    void set_bsp_event(types::board_support_common::Event event) {
        sim_data.actual_bsp_event = event;
    }

    void set_pp(types::board_support_common::Ampacity pp) {
        sim_data.pp = pp;
    }

    void set_rcd_current(float rcd_current) {
        sim_data.rcd_current_ma = rcd_current;
    }

    void set_pwm_duty_cycle(float pwm_duty_cycle) {
        sim_data.pwm_duty_cycle = pwm_duty_cycle;
    }

    void set_slac_state(types::slac::State slac_state) {
        sim_data.slac_state = slac_state;
    }

    types::slac::State get_slac_state() const {
        return sim_data.slac_state;
    }

    /// Stop a running matching process and leave the stack unmatched
    /// ([V2G3-A09-123]/[V2G3-A09-126]).
    void stop_matching();

    /// Whether the measured control pilot is in Bx/Cx/Dx, the only states in
    /// which a matching process may run ([V2G3-M06-13]/[V2G3-A09-123]).
    bool cp_state_allows_matching() const;

    void set_iso_pwr_ready(bool iso_pwr_ready) {
        sim_data.iso_pwr_ready = iso_pwr_ready;
    }

    void set_evse_max_current(size_t evse_max_current) {
        sim_data.evse_maxcurrent = evse_max_current;
    }

    void set_iso_stopped(bool iso_stopped) {
        sim_data.iso_stopped = iso_stopped;
    }

    void set_iso_charger_paused(bool iso_charger_paused) {
        sim_data.iso_charger_paused = iso_charger_paused;
    }

    void set_v2g_finished(bool v2g_finished) {
        sim_data.v2g_finished = v2g_finished;
    }

    // V2G ended with the contactor closed: open it and fall back to State B ([V2G2-526],
    // [V2G2-728]).
    void end_charging_session() {
        switch (sim_data.state) {
        case SimState::CHARGING_REGULATED:
        case SimState::CHARGING_FIXED:
        case SimState::ISO_POWER_READY:
        case SimState::ISO_CHARGING_REGULATED:
            EVLOG_info << "V2G session ended - opening the contactor and returning the control pilot to state B";
            sim_data.dc_power_on = false;
            sim_data.state = SimState::PLUGGED_IN;
            break;
        default:
            // UNPLUGGED / PLUGGED_IN need nothing; ERROR_E, DIODE_FAIL and BCB_TOGGLE are
            // deliberate pilot states the session end must not override.
            break;
        }
    }

    void set_dc_power_on(bool dc_power_on) {
        sim_data.dc_power_on = dc_power_on;
    }

    void state_machine();
    bool sleep(const CmdArguments&, size_t);
    bool cp_c_pulse(const CmdArguments&, size_t);
    bool iec_wait_pwr_ready(const CmdArguments&);
    bool iso_wait_pwm_is_running(const CmdArguments&, size_t loop_interval_ms);
    bool draw_power_regulated(const CmdArguments&);
    bool draw_power_fixed(const CmdArguments&);
    bool pause(const CmdArguments&);
    bool unplug(const CmdArguments&);
    bool error_e(const CmdArguments&);
    bool diode_fail(const CmdArguments&);
    bool rcd_current(const CmdArguments&);
    bool iso_wait_slac_matched(const CmdArguments&);
    bool iso_wait_pwr_ready(const CmdArguments&);
    bool iso_dc_power_on(const CmdArguments&);
    bool iso_start_v2g_session(const CmdArguments&, bool);
    bool iso_draw_power_regulated(const CmdArguments&);
    bool iso_stop_charging(const CmdArguments&);
    bool iso_wait_for_stop(const CmdArguments&, size_t);
    bool iso_wait_v2g_session_stopped(const CmdArguments&);
    bool iso_pause_charging(const CmdArguments&);
    bool iso_wait_for_resume(const CmdArguments&);
    bool iso_start_bcb_toggle(const CmdArguments&);
    bool wait_for_real_plugin(const CmdArguments&);

private:
    SimulationData sim_data;
    const module::Conf& config;
    std::chrono::time_point<std::chrono::steady_clock> timepoint_last_update;
    double charge_current_a{0};

    double latest_soc{0};
    // Last values pushed through ISO15118_ev::update_present_values, so a tick that changes
    // nothing does not re-send. Unset until the first tick that has something to report.
    std::optional<double> latest_present_voltage;
    std::optional<double> latest_present_active_power;

    enum class ChargeMode {
        None,
        AC,
        ACThreePhase,
        DC,
    } charge_mode{ChargeMode::None};

    const std::unique_ptr<ev_board_supportIntf>& r_ev_board_support;
    const std::vector<std::unique_ptr<ISO15118_evIntf>>& r_ev;
    const std::vector<std::unique_ptr<ev_slacIntf>>& r_slac;
    const std::unique_ptr<ev_managerImplBase>& p_ev_manager;

    void simulate_soc();
};
