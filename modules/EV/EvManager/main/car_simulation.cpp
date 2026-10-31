// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "car_simulation.hpp"

#include "constants.hpp"
#include "simulation_command.hpp"

#include <algorithm>

#include <everest/logging.hpp>
#include <fmt/format.h>

constexpr double MS_FACTOR = (1.0 / 60.0 / 60.0 / 1000.0);

void CarSimulation::state_machine() {
    using types::ev_board_support::EvCpState;

    const auto state_has_changed = sim_data.state != sim_data.last_state;
    sim_data.last_state = sim_data.state;

    switch (sim_data.state) {
    case SimState::UNPLUGGED:
        if (state_has_changed) {
            EVLOG_info << "<<< UNPLUGGED: resetting the vehicle - CP state A (resistor released), power off, "
                          "SLAC unmatched, charging stopped";

            r_ev_board_support->call_set_cp_state(EvCpState::A);
            r_ev_board_support->call_allow_power_on(false);
            // Wait for physical plugin (ev BSP sees state A on CP and not Disconnected)

            // [V2G3-A09-126]: stop matching on plug out; the SLAC stack otherwise repeats it
            // for TT_matching_repetition.
            stop_matching();
            if (!r_ev.empty()) {
                r_ev[0]->call_stop_charging();
            }
            EVLOG_info << "Vehicle reset complete - ready for the next plug-in";
        }
        break;
    case SimState::PLUGGED_IN:
        if (state_has_changed) {
            r_ev_board_support->call_set_cp_state(EvCpState::B);
            r_ev_board_support->call_allow_power_on(false);
            simulate_soc();
        }
        break;
    case SimState::CHARGING_REGULATED:
        if (state_has_changed || sim_data.pwm_duty_cycle != sim_data.last_pwm_duty_cycle) {
            sim_data.last_pwm_duty_cycle = sim_data.pwm_duty_cycle;
            // do not draw power if EVSE paused by stopping PWM
            if (sim_data.pwm_duty_cycle > 7.0 && sim_data.pwm_duty_cycle < 97.0) {
                r_ev_board_support->call_set_cp_state(EvCpState::C);
                r_ev_board_support->call_allow_power_on(true);
            } else {
                r_ev_board_support->call_set_cp_state(EvCpState::B);
                r_ev_board_support->call_allow_power_on(false);
            }
        }
        break;
    case SimState::CHARGING_FIXED:
        // Todo(sl): What to do here
        if (state_has_changed) {
            // Also draw power if EVSE stopped PWM - this is a break the rules simulator->mode to test the charging
            // implementation!
            r_ev_board_support->call_set_cp_state(EvCpState::C);
            r_ev_board_support->call_allow_power_on(true);
        }
        break;

    case SimState::ERROR_E:
        if (state_has_changed) {
            r_ev_board_support->call_set_cp_state(EvCpState::E);
            r_ev_board_support->call_allow_power_on(false);
        }
        break;
    case SimState::DIODE_FAIL:
        if (state_has_changed) {
            r_ev_board_support->call_diode_fail(true);
            r_ev_board_support->call_allow_power_on(false);
        }
        break;
    case SimState::ISO_POWER_READY:
        if (state_has_changed) {
            r_ev_board_support->call_set_cp_state(EvCpState::C);
        }
        break;
    case SimState::ISO_CHARGING_REGULATED:
        if (state_has_changed) {
            r_ev_board_support->call_set_cp_state(EvCpState::C);
            r_ev_board_support->call_allow_power_on(true);
        }
        break;
    case SimState::BCB_TOGGLE:
        if (sim_data.bcb_toggle_C) {
            r_ev_board_support->call_set_cp_state(EvCpState::C);
            sim_data.bcb_toggle_C = false;
        } else {
            r_ev_board_support->call_set_cp_state(EvCpState::B);
            sim_data.bcb_toggle_C = true;
            ++sim_data.bcb_toggles;
        }
        break;
    default:
        sim_data.state = SimState::UNPLUGGED;
        break;
    }

    if (not state_has_changed and
        (sim_data.state == SimState::CHARGING_REGULATED or sim_data.state == SimState::CHARGING_FIXED or
         sim_data.state == SimState::ISO_CHARGING_REGULATED)) {
        simulate_soc();
    }
    timepoint_last_update = std::chrono::steady_clock::now();
};

bool CarSimulation::cp_state_allows_matching() const {
    using types::board_support_common::Event;
    const auto cp = sim_data.actual_bsp_event;
    return cp == Event::B or cp == Event::C or cp == Event::D;
}

void CarSimulation::stop_matching() {
    sim_data.slac_state = types::slac::State::UNMATCHED;
    if (!r_slac.empty()) {
        r_slac[0]->call_reset();
    }
}

bool CarSimulation::handle_bsp_event(const types::board_support_common::BspEvent& bsp_event) {
    using types::board_support_common::Event;
    // PowerOn / PowerOff are relay (contactor) feedback, not a control pilot measurement, though
    // they arrive on the same variable. They are published to ev_manager subscribers, but neither
    // reach the HLC stack nor replace the measured pilot that cp_state_allows_matching() reads.
    if (bsp_event.event == Event::PowerOn or bsp_event.event == Event::PowerOff) {
        EVLOG_debug << "Relay feedback from the vehicle BSP: " << bsp_event.event;
        p_ev_manager->publish_bsp_event(bsp_event);
        return false;
    }
    bool session_ended = false;
    const auto previous_event = sim_data.actual_bsp_event;
    sim_data.actual_bsp_event = bsp_event.event;
    if (bsp_event.event != previous_event) {
        EVLOG_info << "CP measured at the vehicle inlet: " << previous_event << " -> " << bsp_event.event;
        forward_cp_state_to_hlc(bsp_event.event);
    }
    // [V2G3-A09-123]: E/F stops matching, like a plug out.
    if (bsp_event.event == Event::E or bsp_event.event == Event::F) {
        if (sim_data.slac_state != types::slac::State::UNMATCHED) {
            EVLOG_info << "CP left the Bx/Cx/Dx range (" << bsp_event.event << ") - stopping the matching process";
            stop_matching();
        }
        // E/F applied by the vehicle itself (error_e, diode_fail) is not a session end.
        const auto state = sim_data.state;
        if (state != SimState::UNPLUGGED and state != SimState::ERROR_E and state != SimState::DIODE_FAIL) {
            // E/F ends the session; UNMATCHED re-arms iso_wait_slac_matched.
            EVLOG_info << "<<< SESSION ABORTED by CP state " << bsp_event.event << " - resetting the vehicle";
            // abort, not stop: [V2G2-025]/[V2G2-728] want no SessionStop exchange.
            abort_v2g_session();
            session_ended = true;
        }
    }
    if (bsp_event.event == Event::Disconnected) {
        if (sim_data.state != SimState::UNPLUGGED) {
            EVLOG_info << "<<< PLUG-OUT detected (CP dead) - cancelling the charging session and resetting";
            // Same as the E/F case: no pilot left for a SessionStop exchange.
            abort_v2g_session();
            session_ended = true;
        } else {
            // Already unplugged: nothing to cancel.
            EVLOG_info << "Plug-out signalled while already unplugged - no session to cancel";
        }
    }
    p_ev_manager->publish_bsp_event(bsp_event);
    return session_ended;
}

void CarSimulation::forward_cp_state_to_hlc(types::board_support_common::Event event) {
    if (r_ev.empty()) {
        return;
    }
    using BspEvent = types::board_support_common::Event;
    std::optional<types::iso15118::CpState> cp_state;
    switch (event) {
    case BspEvent::A:
        cp_state = types::iso15118::CpState::A;
        break;
    case BspEvent::B:
        cp_state = types::iso15118::CpState::B;
        break;
    case BspEvent::C:
        cp_state = types::iso15118::CpState::C;
        break;
    case BspEvent::D:
        cp_state = types::iso15118::CpState::D;
        break;
    case BspEvent::E:
        cp_state = types::iso15118::CpState::E;
        break;
    case BspEvent::F:
        cp_state = types::iso15118::CpState::F;
        break;
    case BspEvent::PowerOn:
    case BspEvent::PowerOff:
    case BspEvent::Disconnected:
        return; // not control pilot states
    }
    // The HLC stack has no board support connection; [V2G2-847] needs the CP state.
    r_ev.at(0)->call_cp_state_changed(cp_state.value());
}

void CarSimulation::abort_v2g_session() {
    if (r_ev.empty()) {
        return;
    }
    r_ev.at(0)->call_abort_charging();
}

void CarSimulation::simulate_soc() {
    const double ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - timepoint_last_update)
            .count();
    const double factor = MS_FACTOR * ms;
    double power = 0.0;
    types::evse_manager::EVInfo ev_info;
    switch (charge_mode) {
    case ChargeMode::None:
        // nothing to do
        break;
    case ChargeMode::AC:
        power = charge_current_a * config.ac_nominal_voltage;
        ev_info.target_current = charge_current_a;
        ev_info.target_voltage = 0;
        break;
    case ChargeMode::ACThreePhase:
        power = charge_current_a * config.ac_nominal_voltage * 3.0;
        ev_info.target_current = charge_current_a;
        ev_info.target_voltage = 0;
        break;
    case ChargeMode::DC:
        power = config.dc_target_current * config.dc_target_voltage;
        ev_info.target_current = config.dc_target_current;
        ev_info.target_voltage = config.dc_target_voltage;
        break;
    }

    if (sim_data.battery_charge_wh > sim_data.battery_capacity_wh) {
        sim_data.battery_charge_wh = sim_data.battery_capacity_wh;
    } else {
        sim_data.battery_charge_wh += power * factor;
    }

    auto soc = (sim_data.battery_charge_wh / sim_data.battery_capacity_wh) * 100.0;

    if (soc > 100.0) {
        soc = 100.0;
    } else if (soc <= 0.0) {
        soc = 0.0;
    }

    if (latest_soc != soc) {
        latest_soc = soc;

        if (!r_ev.empty()) {
            r_ev[0]->call_update_soc(soc);
        }
    }

    if (soc < 100.0) {
        sim_data.full_soc_pause_requested = false;
    } else if (config.pause_charging_at_full_soc and not sim_data.full_soc_pause_requested) {
        sim_data.full_soc_pause_requested = true;
        EVLOG_info << "Simulated battery is full, pausing charging";
        if (sim_data.state == SimState::ISO_CHARGING_REGULATED) {
            // Run the pause through the command queue so it behaves exactly like
            // the iso_pause_charging simulation command, replacing whatever
            // session commands are still running.
            sim_data.modify_charging_session_cmds.emplace("iso_pause_charging");
        } else {
            sim_data.state = SimState::PLUGGED_IN;
        }
    }

    // The values the simulated EV "measures". Kept here, not in the protocol stack,
    // since the stack must not invent a measurement but a simulator may.
    std::optional<double> present_voltage;
    std::optional<double> present_active_power;
    switch (charge_mode) {
    case ChargeMode::None:
        break;
    case ChargeMode::AC:
    case ChargeMode::ACThreePhase:
        present_active_power = power;
        break;
    case ChargeMode::DC:
        present_voltage = config.dc_target_voltage;
        present_active_power = power;
        break;
    }

    if (present_voltage != latest_present_voltage or present_active_power != latest_present_active_power) {
        latest_present_voltage = present_voltage;
        latest_present_active_power = present_active_power;

        if (!r_ev.empty() and (present_voltage.has_value() or present_active_power.has_value())) {
            types::iso15118::EvPresentValues values;
            values.present_voltage = present_voltage;
            values.present_active_power = present_active_power;
            r_ev[0]->call_update_present_values(values);
        }
    }

    ev_info.soc = soc;
    ev_info.battery_capacity = sim_data.battery_capacity_wh;
    ev_info.battery_full_soc = 100;

    p_ev_manager->publish_ev_info(ev_info);
}

// Hold CP state C for the given number of seconds, then release it. On CCS this is a
// (non-standard-length) state-C pulse; its real purpose is the MCS EV role, where the firmware
// turns a readiness claim held while the EVSE idles in B0 into the IEC 61851-23-3 CC.5.2.4
// wake-up toggle (S V3 pulses) - the way a mated EV asks a sleeping EVSE for a new session.
// The firmware needs the claim to outlast its >= 1 s entry wait, so hold for at least 2 s;
// 4 s spans the entry wait plus one full 2 s pulse.
bool CarSimulation::cp_c_pulse(const CmdArguments& arguments, size_t loop_interval_ms) {
    if (not sim_data.cp_c_pulse_ticks_left.has_value()) {
        const auto hold_time_ms = std::stold(arguments[0]) * 1000;
        sim_data.cp_c_pulse_ticks_left = static_cast<size_t>(hold_time_ms / loop_interval_ms) + 1;
        r_ev_board_support->call_set_cp_state(types::ev_board_support::EvCpState::C);
    }
    auto& ticks_left = sim_data.cp_c_pulse_ticks_left.value();
    ticks_left -= 1;
    if (not(ticks_left > 0)) {
        release_cp_c_pulse();
        sim_data.cp_c_pulse_ticks_left.reset();
        return true;
    }
    return false;
}

bool CarSimulation::sleep(const CmdArguments& arguments, size_t loop_interval_ms) {
    if (not sim_data.sleep_ticks_left.has_value()) {
        const auto sleep_time = std::stold(arguments[0]);
        const auto sleep_time_ms = sleep_time * 1000;
        sim_data.sleep_ticks_left = static_cast<long long>(sleep_time_ms / loop_interval_ms) + 1;
    }
    auto& sleep_ticks_left = sim_data.sleep_ticks_left.value();
    sleep_ticks_left -= 1;
    if (not(sleep_ticks_left > 0)) {
        sim_data.sleep_ticks_left.reset();
        return true;
    } else {
        return false;
    }
}

bool CarSimulation::iec_wait_pwr_ready(const CmdArguments& arguments) {
    sim_data.state = SimState::PLUGGED_IN;
    return (sim_data.pwm_duty_cycle > 7.0f && sim_data.pwm_duty_cycle < 97.0f);
}

bool CarSimulation::iso_wait_pwm_is_running(const CmdArguments& arguments, size_t loop_interval_ms) {
    sim_data.state = SimState::PLUGGED_IN;
    if (sim_data.pwm_duty_cycle > 4.0f && sim_data.pwm_duty_cycle < 97.0f) {
        sim_data.pwm_wait_ticks_left.reset();
        return true;
    }
    if (arguments.empty()) {
        // No fallback requested: wait for the PWM indefinitely (default).
        return false;
    }
    // Optional fallback: [V2G3-M06-13] starts matching on the CP transition alone, so a static
    // +12 V EVSE must not stall the EV.
    if (not sim_data.pwm_wait_ticks_left.has_value()) {
        const auto timeout_ms = std::stold(arguments[0]) * 1000;
        sim_data.pwm_wait_ticks_left = static_cast<size_t>(timeout_ms / loop_interval_ms) + 1;
    }
    auto& ticks_left = sim_data.pwm_wait_ticks_left.value();
    ticks_left -= 1;
    if (ticks_left > 0) {
        return false;
    }
    sim_data.pwm_wait_ticks_left.reset();
    EVLOG_info << "iso_wait_pwm_is_running: no PWM within " << arguments[0]
               << " s, starting the matching process on the CP state alone";
    return true;
}

bool CarSimulation::draw_power_regulated(const CmdArguments& arguments) {
    charge_current_a = std::stod(arguments[0]);
    r_ev_board_support->call_set_ac_max_current(charge_current_a);
    if (arguments[1] == constants::THREE_PHASES) {
        r_ev_board_support->call_set_three_phases(true);
        charge_mode = ChargeMode::ACThreePhase;
    } else {
        r_ev_board_support->call_set_three_phases(false);
        charge_mode = ChargeMode::AC;
    }
    sim_data.state = SimState::CHARGING_REGULATED;
    return true;
}

bool CarSimulation::draw_power_fixed(const CmdArguments& arguments) {
    charge_current_a = std::stod(arguments[0]);
    r_ev_board_support->call_set_ac_max_current(charge_current_a);
    if (arguments[1] == constants::THREE_PHASES) {
        r_ev_board_support->call_set_three_phases(true);
        charge_mode = ChargeMode::ACThreePhase;
    } else {
        r_ev_board_support->call_set_three_phases(false);
        charge_mode = ChargeMode::AC;
    }
    sim_data.state = SimState::CHARGING_FIXED;
    return true;
}

bool CarSimulation::pause(const CmdArguments& arguments) {
    sim_data.state = SimState::PLUGGED_IN;
    return true;
}

bool CarSimulation::unplug(const CmdArguments& arguments) {
    begin_unplug();
    charge_mode = ChargeMode::None;
    return true;
}

bool CarSimulation::error_e(const CmdArguments& arguments) {
    sim_data.state = SimState::ERROR_E;
    return true;
}

bool CarSimulation::diode_fail(const CmdArguments& arguments) {
    sim_data.state = SimState::DIODE_FAIL;
    return true;
}

bool CarSimulation::rcd_current(const CmdArguments& arguments) {
    sim_data.rcd_current_ma = std::stof(arguments[0]);
    r_ev_board_support->call_set_rcd_error(sim_data.rcd_current_ma);
    return true;
}

bool CarSimulation::iso_wait_slac_matched(const CmdArguments& arguments) {
    sim_data.state = SimState::PLUGGED_IN;

    if (sim_data.slac_state == types::slac::State::UNMATCHED) {
        EVLOG_debug << "Slac UNMATCHED";
        // [V2G3-A09-123]: only repeat matching while the pilot is in Bx/Cx/Dx.
        if (!r_slac.empty() and cp_state_allows_matching()) {
            EVLOG_debug << "Slac trigger matching";
            // MATCHING must be recorded BEFORE the calls: on a link that is already up (MCS --
            // carrier present, nothing to negotiate) the provider answers MATCHED within
            // milliseconds, and the state subscription can deliver it while
            // call_trigger_matching() is still on the stack. Assigning afterwards clobbered
            // that MATCHED with MATCHING and the wait never completed -- the provider publishes
            // no second event for a state it already holds. With the assignment first, every
            // provider publish (reset's UNMATCHED included) supersedes this value in publish
            // order, so the provider stays the source of truth.
            sim_data.slac_state = types::slac::State::MATCHING;
            r_slac[0]->call_reset();
            r_slac[0]->call_trigger_matching();
        }
    }
    if (sim_data.slac_state == types::slac::State::MATCHED) {
        EVLOG_debug << "Slac Matched";
        return true;
    }
    return false;
}

bool CarSimulation::iso_wait_pwr_ready(const CmdArguments& arguments) {
    // not iso_charger_paused: a charger pause ends the session too, but is not a failure.
    if (not sim_data.v2g_session_active and not sim_data.iso_charger_paused) {
        throw TerminalCommandFailure{"V2G session ended before the EV was ready for power"};
    }
    if (sim_data.iso_pwr_ready) {
        sim_data.state = SimState::ISO_POWER_READY;
        return true;
    }
    return false;
}

bool CarSimulation::iso_dc_power_on(const CmdArguments& arguments) {
    if (sim_data.iso_stopped) {
        arm_restart_after_charger_stop();
        return false;
    }
    // not iso_charger_paused: a pause ends the session too, but is resumed by the branch below.
    if (not sim_data.v2g_session_active and not sim_data.iso_charger_paused) {
        // Session already ended: stay in State B and let the sequence run on. Falling through
        // would re-assert ISO_POWER_READY and drive the pilot back to C.
        EVLOG_info << "V2G session already ended - not powering on";
        return true;
    }
    sim_data.state = SimState::ISO_POWER_READY;
    if (sim_data.dc_power_on) {
        sim_data.state = SimState::ISO_CHARGING_REGULATED;
        r_ev_board_support->call_allow_power_on(true);
        charge_mode = ChargeMode::DC;
        return true;
    }

    // Return false so the session commands can be adjusted before the simulation ends.
    if (sim_data.iso_charger_paused) {
        arm_resume_after_charger_pause();
    }

    return false;
}

bool CarSimulation::iso_start_v2g_session(const CmdArguments& arguments, bool three_phases) {
    const auto& energy_mode = arguments[0];
    const auto& payment_option = arguments[1];
    const auto& departure_time = std::stoi(arguments[2]);
    const auto& e_amount = std::stoi(arguments[3]);

    const auto selected_payment_option = [payment_option,
                                          this](bool auto_payment_option) -> types::iso15118::SelectedPaymentOption {
        if (auto_payment_option) {
            return types::iso15118::SelectedPaymentOption{};
        }
        auto selected_payment_option =
            types::iso15118::SelectedPaymentOption{std::nullopt, config.force_payment_option};

        if (payment_option == "externalpayment") {
            selected_payment_option.payment_option = types::iso15118::PaymentOption::ExternalPayment;
        } else if (payment_option == "contract") {
            selected_payment_option.payment_option = types::iso15118::PaymentOption::Contract;
        } else {
            EVLOG_warning << "Go back to auto payment because payment_option was not recognized";
            selected_payment_option.enforce_payment_option.reset();
        }

        return selected_payment_option;
    }(payment_option == "auto");

    using types::iso15118::EnergyTransferMode;
    EnergyTransferMode transfer_mode{};
    EnergyMode session_energy_mode{};
    ChargeMode session_charge_mode{};
    if (energy_mode == constants::AC) {
        transfer_mode =
            three_phases ? EnergyTransferMode::AC_three_phase_core : EnergyTransferMode::AC_single_phase_core;
        session_energy_mode = EnergyMode::AC;
        session_charge_mode = three_phases ? ChargeMode::ACThreePhase : ChargeMode::AC;
    } else if (energy_mode == constants::AC_BPT) {
        transfer_mode = EnergyTransferMode::AC_BPT;
        session_energy_mode = EnergyMode::AC;
        session_charge_mode = three_phases ? ChargeMode::ACThreePhase : ChargeMode::AC;
    } else if (energy_mode == constants::AC_DER) {
        transfer_mode = EnergyTransferMode::AC_DER_IEC;
        session_energy_mode = EnergyMode::AC;
        session_charge_mode = three_phases ? ChargeMode::ACThreePhase : ChargeMode::AC;
    } else if (energy_mode == constants::DC) {
        transfer_mode = EnergyTransferMode::DC_extended;
        session_energy_mode = EnergyMode::DC;
        session_charge_mode = ChargeMode::DC;
    } else if (energy_mode == constants::DC_BPT) {
        transfer_mode = EnergyTransferMode::DC_BPT;
        session_energy_mode = EnergyMode::DC;
        session_charge_mode = ChargeMode::DC;
    } else if (energy_mode == constants::MCS) {
        // MCS rides the DC power path on the vehicle side; only the -20 energy service differs.
        transfer_mode = EnergyTransferMode::MCS;
        session_energy_mode = EnergyMode::DC;
        session_charge_mode = ChargeMode::DC;
    } else {
        throw TerminalCommandFailure{"Unknown energy mode '" + energy_mode +
                                     "' (valid: ac, ac_bpt, ac_der, dc, dc_bpt, mcs)"};
    }

    // A rejected start publishes no v2g_session_finished, so nothing would release a wait on this
    // session. A session accepted earlier and still running keeps its bookkeeping.
    if (not r_ev[0]->call_start_charging(transfer_mode, selected_payment_option, departure_time, e_amount)) {
        throw TerminalCommandFailure{"ISO15118_ev rejected the start of a V2G session in energy mode '" + energy_mode +
                                     "'"};
    }

    // Clear the per-session latches; a stale one short-circuits the matching iso_wait_* command.
    sim_data.iso_pwr_ready = false;
    sim_data.iso_stopped = false;
    sim_data.power_delivery_stopped = false;
    sim_data.iso_charger_paused = false;
    sim_data.dc_power_on = false;
    sim_data.stop_hold_ticks_left.reset();
    sim_data.stop_dwell_ticks_left.reset();
    sim_data.v2g_start = AcceptedV2gStart{arguments, session_energy_mode, transfer_mode};
    charge_mode = session_charge_mode;
    // No current drawn yet in this session; a resume or restart before a draw falls back to max_current.
    charge_current_a = 0;
    sim_data.v2g_session_active = true;
    return true;
}

bool CarSimulation::iso_draw_power_regulated(const CmdArguments& arguments) {
    charge_current_a = std::stod(arguments[0]);
    r_ev_board_support->call_set_ac_max_current(charge_current_a);
    if (arguments[1] == constants::THREE_PHASES) {
        r_ev_board_support->call_set_three_phases(true);
        charge_mode = ChargeMode::ACThreePhase;
    } else {
        r_ev_board_support->call_set_three_phases(false);
        charge_mode = ChargeMode::AC;
    }
    sim_data.state = SimState::ISO_CHARGING_REGULATED;
    return true;
}

bool CarSimulation::iso_stop_charging(const CmdArguments& arguments) {
    r_ev[0]->call_stop_charging();
    r_ev_board_support->call_allow_power_on(false);
    if (sim_data.state != SimState::UNPLUGGED) {
        sim_data.state = SimState::PLUGGED_IN;
    }
    charge_mode = ChargeMode::None;
    return true;
}

bool CarSimulation::iso_wait_for_stop(const CmdArguments& arguments, size_t loop_interval_ms) {
    if (sim_data.stop_dwell_ticks_left.has_value()) {
        auto& dwell_ticks_left = sim_data.stop_dwell_ticks_left.value();
        dwell_ticks_left -= 1;
        if (dwell_ticks_left > 0) {
            return false;
        }
        sim_data.stop_dwell_ticks_left.reset();
        if (sim_data.iso_stopped) {
            arm_restart_after_charger_stop();
            return false;
        }
        return true;
    }

    // MCS C-exit under power latches CEFAULT on the bench; release after an accepted Stop
    // has not been verified there, so keep C until session end or the hold budget expires.
    if (sim_data.stop_hold_ticks_left.has_value()) {
        auto& hold_ticks_left = sim_data.stop_hold_ticks_left.value();
        hold_ticks_left -= 1;
        const bool keep_holding = sim_data.v2g_session_active and
                                  (sim_data.v2g_start.transfer_mode == types::iso15118::EnergyTransferMode::MCS or
                                   not sim_data.power_delivery_stopped);
        if (keep_holding and hold_ticks_left > 0) {
            return false;
        }
        if (keep_holding) {
            EVLOG_warning << "V2G session did not wind down within the stop hold budget - "
                             "dropping CP to B with the session still open";
        }
        r_ev_board_support->call_allow_power_on(false);
        sim_data.state = SimState::PLUGGED_IN;
        sim_data.sleep_ticks_left.reset();
        sim_data.stop_hold_ticks_left.reset();
        sim_data.stop_dwell_ticks_left = std::max<size_t>(1, constants::STOP_DWELL_MS / loop_interval_ms);
        return false;
    }

    if (not sim_data.sleep_ticks_left.has_value()) {
        const auto sleep_time_ms = std::stold(arguments[0]) * 1000;
        sim_data.sleep_ticks_left = static_cast<long long>(sleep_time_ms / loop_interval_ms) + 1;
    }
    auto& sleep_ticks_left = sim_data.sleep_ticks_left.value();
    sleep_ticks_left -= 1;
    if (not(sleep_ticks_left > 0) or sim_data.iso_stopped) {
        if (sim_data.iso_stopped) {
            EVLOG_info << "Charger requested stop - sending PowerDelivery(stop), holding CP C";
        }
        r_ev[0]->call_stop_charging();
        sim_data.stop_hold_ticks_left = static_cast<size_t>(constants::STOP_HOLD_BUDGET_MS / loop_interval_ms) + 1;
        return false;
    }
    if (sim_data.iso_charger_paused) {
        arm_resume_after_charger_pause();
        return false;
    }
    return false;
}

// After a resumed or restarted start: power up the way the accepted energy mode does, then charge
// until a stop. AC replays the current drawn in this session, or the configured maximum if none was.
std::string CarSimulation::power_up_commands() const {
    std::string commands{"iso_wait_pwr_ready;"};
    if (sim_data.v2g_start.energy_mode == EnergyMode::DC) {
        commands += "iso_dc_power_on;";
    } else {
        const auto current = charge_current_a > 0 ? charge_current_a : config.max_current;
        const std::string phases = (charge_mode == ChargeMode::ACThreePhase) ? "3" : "1";
        commands += "iso_draw_power_regulated " + fmt::format("{:g}", current) + "," + phases + ";";
    }
    return commands + "iso_wait_for_stop 36000";
}

// ISO 15118-20 charger pause: the link stays matched, so wait for PWM and resume the session.
void CarSimulation::arm_resume_after_charger_pause() {
    EVLOG_info << "Charger wants to pause the session";
    r_ev_board_support->call_allow_power_on(false);

    auto& modify_session_cmds = sim_data.modify_charging_session_cmds.emplace();

    modify_session_cmds = "pause;iso_wait_v2g_session_stopped;sleep 2;iso_wait_pwm_is_running;";
    modify_session_cmds += sim_data.v2g_start.command() + ";";
    modify_session_cmds += power_up_commands();

    sim_data.iso_pwr_ready = false;
    sim_data.dc_power_on = false;
    sim_data.sleep_ticks_left.reset();
    sim_data.iso_charger_paused = false;
}

// DIN SPEC and ISO 15118-2 have no charger-side pause: the charger terminates the V2G session
// and wakes the EV up again via PWM. The wake-up requires a full new SLAC matching and V2G
// session, so stay plugged in and re-arm the whole session start instead of ending the simulation.
void CarSimulation::arm_restart_after_charger_stop() {
    EVLOG_info << "Charger stopped the session, waiting for a PWM wake-up to start a new one";
    r_ev_board_support->call_allow_power_on(false);
    sim_data.state = SimState::PLUGGED_IN;

    auto& modify_session_cmds = sim_data.modify_charging_session_cmds.emplace();

    modify_session_cmds = "pause;iso_wait_v2g_session_stopped;sleep 2;iso_wait_pwm_is_running;";
    if (not r_slac.empty()) {
        modify_session_cmds += "iso_wait_slac_matched;";
    }
    modify_session_cmds += sim_data.v2g_start.command() + ";";
    modify_session_cmds += power_up_commands();

    // Force iso_wait_slac_matched to trigger a new matching: the EV slac sim still reports MATCHED,
    // but the charger restarts its matching on resume and only pairs with a peer in MATCHING state
    sim_data.slac_state = types::slac::State::UNMATCHED;
    sim_data.iso_stopped = false;
    sim_data.iso_pwr_ready = false;
    sim_data.dc_power_on = false;
    sim_data.sleep_ticks_left.reset();
}

bool CarSimulation::iso_wait_v2g_session_stopped(const CmdArguments& arguments) {
    return not sim_data.v2g_session_active;
}

bool CarSimulation::iso_pause_charging(const CmdArguments& arguments) {
    r_ev[0]->call_pause_charging();
    sim_data.state = SimState::PLUGGED_IN;
    sim_data.iso_pwr_ready = false;
    return true;
}

bool CarSimulation::iso_wait_for_resume(const CmdArguments& arguments) {
    return false;
}

bool CarSimulation::iso_start_bcb_toggle(const CmdArguments& arguments) {
    sim_data.state = SimState::BCB_TOGGLE;
    if (sim_data.bcb_toggles >= std::stoul(arguments[0]) || sim_data.bcb_toggles == 3) {
        sim_data.bcb_toggles = 0;
        sim_data.state = SimState::PLUGGED_IN;
        return true;
    }
    return false;
}

bool CarSimulation::wait_for_real_plugin(const CmdArguments& arguments) {
    using types::board_support_common::Event;
    if (sim_data.actual_bsp_event == Event::A) {
        EVLOG_info << ">>> PLUG-IN detected (CP energized, measured state A) - starting charging session";
        sim_data.state = SimState::PLUGGED_IN;
        return true;
    }
    // Log once per distinct CP state.
    if (sim_data.actual_bsp_event != sim_data.last_logged_wait_event) {
        sim_data.last_logged_wait_event = sim_data.actual_bsp_event;
        EVLOG_info << "Waiting for plug-in: CP currently measures " << sim_data.actual_bsp_event
                   << " (plug-in requires state A)";
    }
    return false;
}
