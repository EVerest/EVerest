// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <fmt/format.h>

#include "car_simulatorImpl.hpp"
#include "constants.hpp"
#include "simulation_command.hpp"
#include <everest/logging.hpp>

namespace module::main {

void car_simulatorImpl::init() {
    simulated_already_plugged_in_key = fmt::format("{}_{}_simulated_already_plugged_in", mod->info.name, mod->info.id);
    simulated_plugged_in_command_key = fmt::format("{}_{}_simulated_plugged_in_command", mod->info.name, mod->info.id);
    last_commands = {};
    loop_interval_ms = constants::DEFAULT_LOOP_INTERVAL_MS;
    register_all_commands();

    // Before the subscriptions: their callbacks use car_simulation.
    car_simulation = std::make_unique<CarSimulation>(mod->r_ev_board_support, mod->r_ev, mod->r_slac, mod->p_ev_manager,
                                                     mod->config);

    subscribe_to_variables_on_init();

    std::thread(&car_simulatorImpl::run, this).detach();
}

void car_simulatorImpl::ready() {
    subscribe_to_external_mqtt();

    setup_ev_parameters();

    if (mod->config.keep_cross_boot_plugin_state) {
        if (!mod->r_kvs.empty()) {
            auto plugin_command_variant = mod->r_kvs.at(0)->call_load(simulated_plugged_in_command_key);
            auto* old_plugin_command = std::get_if<std::string>(&plugin_command_variant);
            // If the old plugin command was not stored use this one as a default backup which will require an unplug
            std::string plugin_command{"iec_wait_pwr_ready;sleep 60"};
            if (old_plugin_command != nullptr) {
                plugin_command = *old_plugin_command;
            }

            auto already_plugged_in_variant = mod->r_kvs.at(0)->call_load(simulated_already_plugged_in_key);
            bool* was_plugged_in = std::get_if<bool>(&already_plugged_in_variant);
            if (was_plugged_in != nullptr && *was_plugged_in) {
                enabled = true;
                handle_modify_charging_session(plugin_command);
                plugged_in = true;
                enabled = false;
            }
        }
    }
    if (mod->config.auto_enable) {
        auto enable_copy = mod->config.auto_enable;
        handle_enable(enable_copy);
    }
    if (mod->config.auto_exec) {
        auto value_copy = mod->config.auto_exec_commands;
        handle_execute_charging_session(value_copy);
    }
}

void car_simulatorImpl::handle_enable(bool& value) {
    if (enabled == value) {
        // ignore if value is the same
        EVLOG_warning << "Enabled value didn't change, ignoring enable!";
        return;
    }

    if (!value) {
        const std::lock_guard<std::mutex> lock{car_simulation_mutex};
        if (!car_simulation->is_idle_on_the_wire()) {
            set_execution_active(false);
            // This unplug answers a pilot loss still pending for the next tick.
            cancel_charging_session_flag = false;
            unplug_vehicle_now();
        }
    }

    reset_car_simulation_defaults();

    call_ev_board_support_functions();

    enabled = value;

    mod->r_ev_board_support->call_enable(value);
    if (value) {
        const std::lock_guard<std::mutex> lock{car_simulation_mutex};
        car_simulation->reapply_plugged_in();
    }
    publish_enabled(value);
}

void car_simulatorImpl::handle_execute_charging_session(std::string& value) {
    if (!enabled) {
        EVLOG_warning << "Simulation disabled, cannot execute charging simulation.";
        return;
    }
    const std::lock_guard<std::mutex> lock{car_simulation_mutex};
    execute_charging_session(value);
}

void car_simulatorImpl::execute_charging_session(std::string& value) {
    if (not car_simulation->is_idle_on_the_wire()) {
        EVLOG_warning << "Vehicle is not idle (simulation state " << static_cast<int>(car_simulation->get_state())
                      << "), cannot execute a charging session. Stop the session first or use modify_charging_session.";
        return;
    }
    if (car_simulation->has_live_v2g_session()) {
        EVLOG_warning << "A V2G session is active; stop it first or use modify_charging_session.";
        return;
    }

    if (execution_active) {
        // A parked or waiting queue (typically the 'sleep 36000' park after a session, or a wait
        // that will never come) is replaced, the same way modify_charging_session does it. The
        // vehicle keeps its presence on the wire (see CarSimulation::reset), so on a mated MCS
        // bench this is how the operator starts the next session without a full restart.
        EVLOG_warning << "Replacing the running charging session simulation (it was waiting on: "
                      << (last_blocked_command.empty() ? std::string{"<nothing>"} : last_blocked_command) << ")";
    }

    car_simulation->reset();
    update_command_queue(value);
}

void car_simulatorImpl::handle_modify_charging_session(std::string& value) {
    if (!enabled) {
        EVLOG_warning << "Simulation disabled, cannot execute charging simulation.";
        return;
    }

    const std::lock_guard<std::mutex> lock{car_simulation_mutex};
    update_command_queue(value);
}

void car_simulatorImpl::run() {
    while (true) {
        run_simulation_step();
        std::this_thread::sleep_for(std::chrono::milliseconds(loop_interval_ms));
    }
}

void car_simulatorImpl::run_simulation_step() {
    if (enabled && execution_active) {
        // An execute or modify from another thread may replace the list while this step runs; the
        // list-end handling below then leaves the new list alone.
        std::uint64_t generation{};
        auto progress = run_simulation_loop(generation);
        if (after_simulation_loop) {
            after_simulation_loop();
        }

        bool cancelled = false;
        if (cancel_charging_session_flag) {
            cancel_charging_session_flag = false;
            progress = SequenceProgress::Finished;
            cancelled = true;
        }

        {
            const std::lock_guard<std::mutex> lock{car_simulation_mutex};
            auto& modify_session_cmds = car_simulation->get_modify_charging_session_cmds();
            if (modify_session_cmds.has_value()) {
                auto cmds = modify_session_cmds.value();
                modify_session_cmds.reset();
                update_command_queue(cmds);
                // Queued by the list itself, so it is the same list: a pending cancel still applies.
                generation = list_generation;
            }
        }

        switch (progress) {
        case SequenceProgress::Waiting:
            break;
        case SequenceProgress::Failed: {
            // Not a session end: the vehicle and any V2G session accepted earlier stay as they are,
            // and auto_exec_infinite does not repeat a sequence that cannot run.
            const std::lock_guard<std::mutex> lock{car_simulation_mutex};
            if (list_generation != generation) {
                EVLOG_debug << "A command failed, but a new command list already replaced it - leaving that running";
                break;
            }
            set_execution_active(false);
            EVLOG_info << "Finished simulation. A command failed and ended it";
            if (not car_simulation->is_idle_on_the_wire()) {
                EVLOG_warning << "The vehicle is still active; send a stop or unplug with modify_charging_session";
            }
            break;
        }
        case SequenceProgress::Finished: {
            const std::lock_guard<std::mutex> lock{car_simulation_mutex};
            if (list_generation != generation) {
                // Any cancel belonged to the replaced list.
                EVLOG_debug << "Command list finished, but a new one already replaced it - leaving that running";
                break;
            }
            set_execution_active(false);
            if (cancelled) {
                // The pilot is gone (plug-out) or the EVSE withdrew (E/F): the vehicle is
                // reset NOW, not left pending for the first tick of whatever runs next.
                EVLOG_info << "Finished simulation. Session cancelled - resetting the vehicle";
                unplug_vehicle_now();
            } else {
                EVLOG_info << "Finished simulation. The vehicle keeps its state until a stop or unplug.";
                car_simulation->reset();
            }

            // If we have auto_exec_infinite configured, restart simulation when it is done
            if (mod->config.auto_exec && mod->config.auto_exec_infinite) {
                auto value_copy = mod->config.auto_exec_commands;
                execute_charging_session(value_copy);
            }
            break;
        }
        }
    } else if (cancel_charging_session_flag) {
        // Idle but plugged in (a finished queue leaves the vehicle mated), and the pilot went
        // away: reset the vehicle right here. Left standing, the flag would cancel the next
        // command list the moment it started.
        cancel_charging_session_flag = false;
        EVLOG_info << "Pilot lost while idle - resetting the vehicle";
        apply_vehicle_unplug();
    }
}

void car_simulatorImpl::register_all_commands() {
    command_registry = std::make_unique<CommandRegistry>();

    command_registry->register_command("sleep", 1, [this](const CmdArguments& arguments) {
        return this->car_simulation->sleep(arguments, loop_interval_ms);
    });
    command_registry->register_command("cp_c_pulse", 1, [this](const CmdArguments& arguments) {
        return this->car_simulation->cp_c_pulse(arguments, loop_interval_ms);
    });
    command_registry->register_command("iec_wait_pwr_ready", 0, [this](const CmdArguments& arguments) {
        if (!mod->r_kvs.empty() and not plugged_in) {
            plugged_in = true;
            mod->r_kvs.at(0)->call_store(simulated_already_plugged_in_key, true);
            mod->r_kvs.at(0)->call_store(simulated_plugged_in_command_key, last_commands);
        }
        return this->car_simulation->iec_wait_pwr_ready(arguments);
    });
    command_registry->register_command("iso_wait_pwm_is_running", 0, [this](const CmdArguments& arguments) {
        return this->car_simulation->iso_wait_pwm_is_running(arguments, loop_interval_ms);
    });
    // Same command with an optional fallback timeout in seconds: the EV waits
    // for the EVSE's PWM as usual, but proceeds with the matching process
    // anyway if the duty cycle has not started within that time.
    command_registry->register_command("iso_wait_pwm_is_running", 1, [this](const CmdArguments& arguments) {
        return this->car_simulation->iso_wait_pwm_is_running(arguments, loop_interval_ms);
    });
    command_registry->register_command("draw_power_regulated", 2, [this](const CmdArguments& arguments) {
        return this->car_simulation->draw_power_regulated(arguments);
    });
    command_registry->register_command("draw_power_fixed", 2, [this](const CmdArguments& arguments) {
        return this->car_simulation->draw_power_fixed(arguments);
    });
    command_registry->register_command(
        "pause", 0, [this](const CmdArguments& arguments) { return this->car_simulation->pause(arguments); });
    command_registry->register_command("unplug", 0, [this](const CmdArguments& arguments) {
        if (!mod->r_kvs.empty() and plugged_in) {
            plugged_in = false;
            mod->r_kvs.at(0)->call_store(simulated_already_plugged_in_key, false);
        }
        return this->car_simulation->unplug(arguments);
    });
    command_registry->register_command(
        "error_e", 0, [this](const CmdArguments& arguments) { return this->car_simulation->error_e(arguments); });
    command_registry->register_command(
        "diode_fail", 0, [this](const CmdArguments& arguments) { return this->car_simulation->diode_fail(arguments); });
    command_registry->register_command("rcd_current", 1, [this](const CmdArguments& arguments) {
        return this->car_simulation->rcd_current(arguments);
    });
    command_registry->register_command("iso_draw_power_regulated", 2, [this](const CmdArguments& arguments) {
        return this->car_simulation->iso_draw_power_regulated(arguments);
    });
    command_registry->register_command("wait_for_real_plugin", 0, [this](const CmdArguments& arguments) {
        return this->car_simulation->wait_for_real_plugin(arguments);
    });
    command_registry->register_command("plugin", 0, [this](const CmdArguments& /*arguments*/) {
        if (not mod->config.plugin_commands.empty()) {
            car_simulation->update_modify_charging_session_cmds(mod->config.plugin_commands);
            return false;
        }
        EVLOG_error << "plugin command called but \"plugin_commands\" config key not set";
        return true;
    });
    command_registry->register_command("set_soc", 1, [this](const CmdArguments& arguments) {
        try {
            double soc = std::stod(arguments.at(0));
            this->car_simulation->set_soc(soc);
        } catch (const std::invalid_argument& e) {
            EVLOG_error << "set_soc command called with invalid argument: " << arguments.at(0);
            return true;
        } catch (const std::out_of_range& e) {
            EVLOG_error << "set_soc command called with out of range argument: " << arguments.at(0);
            return true;
        }

        return true;
    });

    if (!mod->r_slac.empty()) {
        command_registry->register_command("iso_wait_slac_matched", 0, [this](const CmdArguments& arguments) {
            if (!mod->r_kvs.empty() and not plugged_in) {
                plugged_in = true;
                mod->r_kvs.at(0)->call_store(simulated_already_plugged_in_key, true);
                mod->r_kvs.at(0)->call_store(simulated_plugged_in_command_key, last_commands);
            }
            return this->car_simulation->iso_wait_slac_matched(arguments);
        });
    }

    if (!mod->r_ev.empty()) {
        command_registry->register_command("iso_wait_pwr_ready", 0, [this](const CmdArguments& arguments) {
            return this->car_simulation->iso_wait_pwr_ready(arguments);
        });
        command_registry->register_command("iso_dc_power_on", 0, [this](const CmdArguments& arguments) {
            return this->car_simulation->iso_dc_power_on(arguments);
        });
        command_registry->register_command("iso_start_v2g_session", 1, [this](const CmdArguments& arguments) {
            constexpr auto payment_option = "auto";
            EVLOG_debug << "Using default payment_options, departure time and eamount";
            CmdArguments args{arguments[0], payment_option, std::to_string(mod->config.departure_time),
                              std::to_string(mod->config.e_amount)};
            return this->car_simulation->iso_start_v2g_session(args, mod->config.three_phases);
        });
        command_registry->register_command("iso_start_v2g_session", 2, [this](const CmdArguments& arguments) {
            EVLOG_debug << "Using default departure time and eamount";
            CmdArguments args{arguments[0], arguments[1], std::to_string(mod->config.departure_time),
                              std::to_string(mod->config.e_amount)};
            return this->car_simulation->iso_start_v2g_session(args, mod->config.three_phases);
        });
        command_registry->register_command("iso_start_v2g_session", 3, [this](const CmdArguments& arguments) {
            constexpr auto payment_option = "auto";
            EVLOG_debug << "Using default payment_options";
            CmdArguments args{arguments[0], payment_option, arguments[1], arguments[2]};
            return this->car_simulation->iso_start_v2g_session(args, mod->config.three_phases);
        });
        command_registry->register_command("iso_start_v2g_session", 4, [this](const CmdArguments& arguments) {
            // 1. Argument: EnergyMode
            // 2. Argument: PaymentOption
            // 3. Argument: DepartureTime
            // 4. Argument: EAmount
            return this->car_simulation->iso_start_v2g_session(arguments, mod->config.three_phases);
        });
        command_registry->register_command("iso_stop_charging", 0, [this](const CmdArguments& arguments) {
            return this->car_simulation->iso_stop_charging(arguments);
        });
        command_registry->register_command("iso_wait_for_stop", 1, [this](const CmdArguments& arguments) {
            return this->car_simulation->iso_wait_for_stop(arguments, loop_interval_ms);
        });
        command_registry->register_command("iso_wait_v2g_session_stopped", 0, [this](const CmdArguments& arguments) {
            return this->car_simulation->iso_wait_v2g_session_stopped(arguments);
        });
        command_registry->register_command("iso_pause_charging", 0, [this](const CmdArguments& arguments) {
            return this->car_simulation->iso_pause_charging(arguments);
        });
        command_registry->register_command("iso_wait_for_resume", 0, [this](const CmdArguments& arguments) {
            return this->car_simulation->iso_wait_for_resume(arguments);
        });
        command_registry->register_command("iso_start_bcb_toggle", 1, [this](const CmdArguments& arguments) {
            return this->car_simulation->iso_start_bcb_toggle(arguments);
        });
    }
}

car_simulatorImpl::SequenceProgress car_simulatorImpl::run_simulation_loop(std::uint64_t& generation) {
    // Execute sim commands until a command blocks, or we are finished
    const std::lock_guard<std::mutex> lock{car_simulation_mutex};
    generation = list_generation;
    while (execution_active && !command_queue.empty()) {
        auto& current_command = command_queue.front();

        auto command_blocked = false;

        try {
            command_blocked = !current_command.execute();
        } catch (const TerminalCommandFailure& e) {
            EVLOG_error << current_command.name() << " failed: " << e.what()
                        << " - dropping the remaining commands of this simulation";
            command_queue = {};
            last_blocked_command.clear();
            car_simulation->get_modify_charging_session_cmds().reset();
            car_simulation->state_machine();
            return SequenceProgress::Failed;
        } catch (const std::exception& e) {
            EVLOG_error << e.what();
        }

        if (!command_blocked) {
            last_blocked_command.clear();
            command_queue.pop();
        } else {
            // Log the blocking command once.
            if (last_blocked_command != current_command.name()) {
                last_blocked_command = current_command.name();
                EVLOG_info << "Simulation waiting on: " << last_blocked_command;
            }
            break; // command blocked, wait for timer to run this function again
        }
    }

    car_simulation->state_machine();

    return command_queue.empty() ? SequenceProgress::Finished : SequenceProgress::Waiting;
}

void car_simulatorImpl::apply_vehicle_unplug() {
    const std::lock_guard<std::mutex> lock{car_simulation_mutex};
    unplug_vehicle_now();
}

void car_simulatorImpl::unplug_vehicle_now() {
    car_simulation->unplug_vehicle();
    // Run the unplug branch immediately: the state machine only ticks while a command list is
    // executing, and a pending reset that fires at the start of the next list is exactly the
    // trap this exists to remove.
    car_simulation->state_machine();
}

void car_simulatorImpl::log_v2g_message(const types::iso15118::V2gMessages& v2g_messages) {
    // Colored like EvseManager's SessionLog EVSE lines. Only responses arrive: the ISO15118_ev
    // provider reports the responses it accepted, never the requests it sent.
    static constexpr auto CYAN = "\033[1;36m";
    static constexpr auto RESET = "\033[1;0m";

    EVLOG_info << CYAN << "EVSE ISO V2G " << types::iso15118::v2g_message_id_to_string(v2g_messages.id) << RESET;
}

void car_simulatorImpl::subscribe_to_variables_on_init() {
    // subscribe bsp_event
    const std::lock_guard<std::mutex> lock{car_simulation_mutex};
    using types::board_support_common::BspEvent;
    mod->r_ev_board_support->subscribe_bsp_event([this](const auto& bsp_event) {
        const std::lock_guard<std::mutex> callback_lock{car_simulation_mutex};
        if (car_simulation->handle_bsp_event(bsp_event)) {
            cancel_charging_session();
        }
    });

    // subscribe bsp_measurement
    using types::board_support_common::BspMeasurement;
    mod->r_ev_board_support->subscribe_bsp_measurement([this](const auto& measurement) {
        car_simulation->set_pp(measurement.proximity_pilot.ampacity);
        car_simulation->set_pwm_duty_cycle(measurement.cp_pwm_duty_cycle);
        if (measurement.rcd_current_mA.has_value()) {
            car_simulation->set_rcd_current(measurement.rcd_current_mA.value());
        }
    });

    // subscribe EVInfo
    using types::evse_manager::EVInfo;
    mod->r_ev_board_support->subscribe_ev_info(
        [this](const auto& ev_info) { mod->p_ev_manager->publish_ev_info(ev_info); });

    // subscribe slac_state
    if (!mod->r_slac.empty()) {
        const auto& slac = mod->r_slac.at(0);
        slac->subscribe_state([this](const auto& state) {
            const std::lock_guard<std::mutex> callback_lock{car_simulation_mutex};
            EVLOG_info << "SLAC state: " << state;
            car_simulation->set_slac_state(state);
        });
    }

    // subscribe the HLC message trace (opt-in console session log)
    if (mod->config.session_logging && !mod->r_ev.empty()) {
        mod->r_ev.at(0)->subscribe_v2g_messages(
            [this](const types::iso15118::V2gMessages& v2g_messages) { log_v2g_message(v2g_messages); });
    }

    // subscribe ev events
    if (!mod->r_ev.empty()) {
        const auto& _ev = mod->r_ev.at(0);
        _ev->subscribe_ev_power_ready([this](auto value) {
            const std::lock_guard<std::mutex> callback_lock{car_simulation_mutex};
            car_simulation->set_iso_pwr_ready(value);
        });
        _ev->subscribe_ac_evse_max_current(
            [this](auto value) { mod->r_ev_board_support->call_set_ac_max_current(value); });
        _ev->subscribe_ac_evse_target_power([this](const types::iso15118::AcTargetPower& value) {
            if (value.target_active_power.has_value()) {
                auto phase_count = mod->config.three_phases ? 3 : 1;
                if (value.target_active_power_L2.has_value() and mod->config.three_phases) {
                    phase_count--;
                }
                if (value.target_active_power_L3.has_value() and mod->config.three_phases) {
                    phase_count--;
                }

                const auto current = value.target_active_power.value() / (mod->config.ac_nominal_voltage * phase_count);
                mod->r_ev_board_support->call_set_ac_max_current(current);
            }
            // TODO(SL): Adding missing reactive power
        });
        _ev->subscribe_stop_from_charger([this]() {
            const std::lock_guard<std::mutex> callback_lock{car_simulation_mutex};
            car_simulation->set_iso_stopped(true);
        });
        _ev->subscribe_power_delivery_stopped([this]() {
            const std::lock_guard<std::mutex> callback_lock{car_simulation_mutex};
            car_simulation->set_power_delivery_stopped(true);
        });
        _ev->subscribe_welding_detection_failed([](const types::iso15118::WeldingDetectionFailure& failure) {
            EVLOG_error << "Welding detection failed after " << failure.cycles << " cycles, last voltage "
                        << (failure.last_voltage_V ? fmt::format("{:g} V", *failure.last_voltage_V) : "unknown");
        });
        _ev->subscribe_v2g_session_finished([this]() {
            const std::lock_guard<std::mutex> callback_lock{car_simulation_mutex};
            car_simulation->set_v2g_session_active(false);
            // Do not stay in C/D with the contactor closed ([V2G2-913]).
            car_simulation->end_charging_session();
            if (not execution_active and car_simulation->get_state() == SimState::PLUGGED_IN) {
                car_simulation->state_machine();
            }
        });
        _ev->subscribe_dc_power_on([this]() {
            const std::lock_guard<std::mutex> callback_lock{car_simulation_mutex};
            car_simulation->set_dc_power_on(true);
        });
        _ev->subscribe_pause_from_charger([this]() {
            const std::lock_guard<std::mutex> callback_lock{car_simulation_mutex};
            car_simulation->set_iso_charger_paused(true);
        });
    }
}

void car_simulatorImpl::setup_ev_parameters() {
    if (!mod->r_ev.empty()) {
        // Assigned by name rather than positionally: the type is generated from
        // types/iso15118.yaml, where adding a property shifts every later member.
        types::iso15118::DcEvParameters dc_params;
        dc_params.max_current_limit = mod->config.dc_max_current_limit;
        dc_params.max_power_limit = mod->config.dc_max_power_limit;
        dc_params.max_voltage_limit = mod->config.dc_max_voltage_limit;
        dc_params.min_voltage_limit = mod->config.dc_min_voltage_limit;
        dc_params.energy_capacity = mod->config.dc_energy_capacity;
        dc_params.target_current = mod->config.dc_target_current;
        dc_params.target_voltage = mod->config.dc_target_voltage;
        mod->r_ev[0]->call_set_dc_params(dc_params);
        if (mod->config.support_sae_j2847) {
            mod->r_ev[0]->call_enable_sae_j2847_v2g_v2h();
            mod->r_ev[0]->call_set_bpt_dc_params(
                {mod->config.dc_discharge_max_current_limit, mod->config.dc_discharge_max_power_limit,
                 mod->config.dc_discharge_target_current, mod->config.dc_discharge_v2g_minimal_soc});
        }
    }
}

void car_simulatorImpl::call_ev_board_support_functions() {
    mod->r_ev_board_support->call_allow_power_on(false);

    mod->r_ev_board_support->call_set_ac_max_current(mod->config.max_current);
    mod->r_ev_board_support->call_set_three_phases(mod->config.three_phases);
}

void car_simulatorImpl::subscribe_to_external_mqtt() {
    const auto& mqtt = mod->mqtt;
    mqtt.subscribe("everest_external/nodered/" + std::to_string(mod->config.connector_id) + "/carsim/cmd/enable",
                   [this](const std::string& message) {
                       auto enable = (message == "true") ? true : false;
                       handle_enable(enable);
                   });
    mqtt.subscribe("everest_external/nodered/" + std::to_string(mod->config.connector_id) +
                       "/carsim/cmd/execute_charging_session",
                   [this](const auto data) {
                       auto data_copy{data};
                       handle_execute_charging_session(data_copy);
                   });
    mqtt.subscribe("everest_external/nodered/" + std::to_string(mod->config.connector_id) +
                       "/carsim/cmd/modify_charging_session",
                   [this](auto data) {
                       auto data_copy = data;
                       handle_modify_charging_session(data_copy);
                   });
}

void car_simulatorImpl::reset_car_simulation_defaults() {
    const std::lock_guard<std::mutex> lock{car_simulation_mutex};
    car_simulation->reset();
}

void car_simulatorImpl::update_command_queue(std::string& value) {
    last_commands = value;
    // The commands of the old queue are gone; their armed countdowns must not survive them
    // (a replaced mid-flight 'sleep 36000' otherwise donates its remaining ticks to the next
    // tick-based command that runs - see SimulationData::clear_command_ticks).
    car_simulation->abort_command_in_flight();
    command_queue = SimulationCommand::parse_sim_commands(value, *command_registry);
    ++list_generation;
    set_execution_active(!command_queue.empty());
}

void car_simulatorImpl::set_execution_active(bool value) {
    execution_active = value;
}

void car_simulatorImpl::cancel_charging_session() {
    cancel_charging_session_flag = true;
}

} // namespace module::main
