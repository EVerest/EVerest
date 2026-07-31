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

    void reset() {
        // The control pilot is the outside world, not simulation state: resetting the vehicle does
        // not change what the wire currently reads. Dropping the last measurement back to
        // Disconnected here would make wait_for_real_plugin -- which is level-triggered on purpose,
        // so a vehicle that comes up on an already-energized pilot still starts a session -- wait
        // for an edge that has already happened and never comes again.
        const auto measured_cp_state = sim_data.actual_bsp_event;
        sim_data = SimulationData();
        sim_data.actual_bsp_event = measured_cp_state;
        sim_data.last_logged_wait_event = measured_cp_state;
        sim_data.battery_capacity_wh = config.dc_energy_capacity;
        double soc = config.soc;
        sim_data.battery_charge_wh = config.dc_energy_capacity * (soc / 100.0);
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

    // The V2G session ended while the vehicle is still plugged in. On the happy path the
    // PowerDelivery(Stop) exchange has already brought us back here, but a session that ends any
    // other way -- fatal ResponseCode, message timeout, pre-charge timeout, a charger that just
    // stops answering -- must not leave the contactor closed and the control pilot at C/D:
    // [V2G2-526] / [V2G2-728] have the EVCC stop the charging process and fall back to State B.
    // Going back to PLUGGED_IN makes the state machine do both.
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
