// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "EvDriverSim.hpp"
#include <everest_api_types/ev_controller/codec.hpp>
#include <everest_api_types/ev_driver_sim/codec.hpp>
#include <everest_api_types/generic/codec.hpp>

namespace module {

void EvDriverSim::init() {
    invoke_init(*p_board_support);
    m_topics.setup(info.id, "ev_driver_sim", 1);
    const auto heartbeat = "everest_api/1/ev_controller/" + config.ev_controller_module_id + "/e2m/heartbeat";
    m_subscriptions.push_back(mqtt.subscribe(heartbeat, [this](const std::string&) {
        auto state = m_state.handle();
        state->controller_seen = true;
        if (state->controller_fault) {
            p_board_support->clear_error("generic/CommunicationFault", "EvControllerHeartbeat");
            state->controller_fault = false;
        }
        m_state.notify_all();
    }));
    const auto command_ack = "everest_api/1/ev_controller/" + config.ev_controller_module_id + "/e2m/command_ack";
    m_subscriptions.push_back(mqtt.subscribe(command_ack, [this](const std::string& payload) {
        auto state = m_state.handle();
        if (!state->ready || state->stopping) {
            return;
        }
        try {
            namespace car = everest::lib::API::V1_0::types::ev_controller;
            car::CommandAck ack{};
            if (!car::adl_deserialize(payload, ack) || ack.command != "configure_session") {
                return;
            }
            const auto rejection =
                ack.status == car::CommandAckStatus::Rejected
                    ? std::make_optional(ack.reason.value_or("EvController rejected configure_session"))
                    : std::nullopt;
            state->scenario.configuration_ack(rejection, scenario_actions(*state));
        } catch (const std::exception& e) {
            fail_scenario(*state, e.what());
        } catch (...) {
            fail_scenario(*state, "unknown exception in configuration acknowledgement");
        }
        m_state.notify_all();
    }));
    r_ev_board_support->subscribe_bsp_event([this](const auto& event) {
        auto state = m_state.handle();
        if (state->ready) {
            apply(state->gate.on_board_event(event));
        }
    });
    r_ev_board_support->subscribe_bsp_measurement([this](const auto& measurement) {
        auto state = m_state.handle();
        if (state->ready && state->gate.passed()) {
            p_board_support->publish_bsp_measurement(measurement);
        }
    });
    r_ev_board_support->subscribe_ev_info([this](const auto& info) {
        auto state = m_state.handle();
        if (state->ready && state->gate.passed()) {
            p_board_support->publish_ev_info(info);
        }
    });
}

void EvDriverSim::ready() {
    invoke_ready(*p_board_support);
    auto state = m_state.handle();
    apply(state->gate.passed() ? state->gate.plug() : state->gate.unplug());
    state->ready = true;
    publish_world_state(*state);
    for (const std::string name :
         {"plug", "unplug", "inject_fault", "clear_fault", "run_scenario", "communication_check"}) {
        m_subscriptions.push_back(mqtt.subscribe(m_topics.extern_to_everest(name), [this, name](const auto& payload) {
            try {
                command(name, payload);
            } catch (const std::exception& e) {
                EVLOG_error << "EvDriverSim " << name << " command_ack publish failed: " << e.what();
            } catch (...) {
                EVLOG_error << "EvDriverSim " << name << " command_ack publish failed: unknown exception";
            }
        }));
    }
    m_startup_watchdog = std::thread([this] {
        auto state = m_state.handle();
        if (!state.wait_for([&] { return state->controller_seen || state->stopping; },
                            std::chrono::seconds(config.heartbeat_wait_s))) {
            const auto error = p_board_support->error_factory->create_error(
                "generic/CommunicationFault", "EvControllerHeartbeat",
                "No heartbeat from EvController '" + config.ev_controller_module_id + "'",
                Everest::error::Severity::High);
            p_board_support->raise_error(error);
            state->controller_fault = true;
        }
    });
    m_scenario_timer = std::thread([this] {
        auto state = m_state.handle();
        while (!state->stopping) {
            const auto deadline = state->scenario.next_deadline();
            if (!deadline) {
                state.wait([&] { return state->stopping || state->scenario.next_deadline().has_value(); });
            } else if (!state.wait_until(
                           *deadline, [&] { return state->stopping || state->scenario.next_deadline() != deadline; })) {
                try {
                    state->scenario.on_timer(scenario_actions(*state));
                } catch (const std::exception& e) {
                    fail_scenario(*state, e.what());
                } catch (...) {
                    fail_scenario(*state, "unknown exception in scenario timer");
                }
            }
        }
    });
    m_comm_check.start(config.cfg_communication_check_to_s);
    m_comm_check.heartbeat(config.cfg_heartbeat_interval_ms, [this] {
        try {
            mqtt.publish(m_topics.everest_to_extern("heartbeat"),
                         everest::lib::API::V1_0::types::generic::serialize(m_heartbeat_id++));
            return true;
        } catch (const std::exception& e) {
            EVLOG_error << "EvDriverSim heartbeat failed: " << e.what();
        } catch (...) {
            EVLOG_error << "EvDriverSim heartbeat failed";
        }
        return false;
    });
}

EvDriverSim::~EvDriverSim() {
    stop();
}

void EvDriverSim::stop() {
    m_comm_check.stop_heartbeat();
    {
        auto state = m_state.handle();
        state->stopping = true;
        state->ready = false;
    }
    m_state.notify_all();
    if (m_scenario_timer.joinable()) {
        m_scenario_timer.join();
    }
    for (auto& unsubscribe : m_subscriptions) {
        if (unsubscribe) {
            unsubscribe();
        }
    }
    m_subscriptions.clear();
    if (m_startup_watchdog.joinable()) {
        m_startup_watchdog.join();
    }
}

void EvDriverSim::publish_world_state(const State& state) {
    mqtt.publish(m_topics.everest_to_extern("world_state"),
                 API_driver::serialize(API_driver::WorldState{state.gate.passed() ? API_driver::PilotState::Passed
                                                                                  : API_driver::PilotState::Withheld,
                                                              state.faults.active()}));
}

void EvDriverSim::command(const std::string& name, const std::string& payload) {
    auto state = m_state.handle();
    const auto scenario_was_active = state->scenario.active();
    std::optional<std::string> rejection;
    try {
        if (name == "run_scenario") {
            API_driver::RunScenarioParams params{};
            if (!API_driver::adl_deserialize(payload, params)) {
                rejection = "bad run_scenario payload";
            } else {
                rejection = state->scenario.start(params, state->controller_seen, state->gate.passed(),
                                                  scenario_actions(*state));
                m_state.notify_all();
            }
        } else if (name != "communication_check") {
            rejection = world_command(*state, name, payload);
        } else if (name == "communication_check") {
            bool value = false;
            if (everest::lib::API::V1_0::types::generic::adl_deserialize(payload, value)) {
                m_comm_check.set_value(value);
            } else {
                rejection = "bad communication_check payload";
            }
        }
    } catch (const std::exception& e) {
        EVLOG_error << "EvDriverSim " << name << " failed: " << e.what();
        rejection = e.what();
        if (name == "run_scenario" && !scenario_was_active && state->scenario.active()) {
            fail_scenario(*state, *rejection);
            m_state.notify_all();
        }
    } catch (...) {
        EVLOG_error << "EvDriverSim " << name << " failed";
        rejection = "unknown command failure";
        if (name == "run_scenario" && !scenario_was_active && state->scenario.active()) {
            fail_scenario(*state, *rejection);
            m_state.notify_all();
        }
    }
    mqtt.publish(m_topics.everest_to_extern("command_ack"),
                 API_driver::serialize(API_driver::CommandAck{
                     name, rejection ? API_driver::CommandAckStatus::Rejected : API_driver::CommandAckStatus::Accepted,
                     rejection}));
}

std::optional<std::string> EvDriverSim::world_command(State& state, const std::string& name,
                                                      const std::string& payload) {
    const auto passed = state.gate.passed();
    const auto fault = state.faults.active();
    std::optional<std::string> rejection;
    if (name == "plug") {
        if (state.gate.passed()) {
            rejection = "already plugged";
        } else {
            apply(state.gate.plug());
        }
    } else if (name == "unplug") {
        if (!state.gate.passed()) {
            rejection = "already unplugged";
        } else {
            state.faults.unplug();
            apply(state.gate.unplug());
        }
    } else if (name == "clear_fault") {
        apply(state.faults.clear());
    } else if (name == "inject_fault") {
        API_driver::InjectFaultParams params{};
        if (!API_driver::adl_deserialize(payload, params)) {
            rejection = "bad inject_fault payload";
        } else {
            const auto out = state.faults.inject(params, state.gate.passed());
            rejection = out.rejection;
            apply(out);
        }
    }

    if (passed != state.gate.passed() || fault != state.faults.active()) {
        publish_world_state(state);
    }
    return rejection;
}

void EvDriverSim::fail_scenario(State& state, const std::string& failure) {
    const auto step = state.scenario.current_step();
    auto reason =
        "scenario step " + step + " for EvController '" + config.ev_controller_module_id + "' failed: " + failure;
    if (step == "plug" || step == "unplug" || step == "inject_fault" || step == "clear_fault") {
        reason += "; world state may be partially applied";
    }
    EVLOG_error << "EvDriverSim " << reason;
    try {
        state.scenario.reject(reason, scenario_actions(state));
    } catch (const std::exception& e) {
        EVLOG_error << "EvDriverSim could not publish scenario rejection: " << reason << "; " << e.what();
    } catch (...) {
        EVLOG_error << "EvDriverSim could not publish scenario rejection: " << reason << "; unknown exception";
    }
}

ScenarioRunner::Actions EvDriverSim::scenario_actions(State& state) {
    return {[this, &state](const std::string& name, const std::string& payload) -> std::optional<std::string> {
                if (name == "plug" || name == "unplug" || name == "inject_fault" || name == "clear_fault") {
                    return world_command(state, name, payload);
                } else {
                    mqtt.publish("everest_api/1/ev_controller/" + config.ev_controller_module_id + "/m2e/" + name,
                                 payload);
                }
                return std::nullopt;
            },
            [this](const API_driver::ScenarioEvent& event) {
                mqtt.publish(m_topics.everest_to_extern("scenario_event"), API_driver::serialize(event));
            }};
}

void EvDriverSim::apply(const WorldFaults::Out& out) {
    if (out.diode_fail) {
        r_ev_board_support->call_diode_fail(*out.diode_fail);
    }
    if (out.rcd_mA) {
        r_ev_board_support->call_set_rcd_error(*out.rcd_mA);
    }
    if (out.to_board_cp) {
        r_ev_board_support->call_set_cp_state(*out.to_board_cp);
    }
}

void EvDriverSim::apply(const PilotGate::Out& out) {
    if (out.to_board_cp) {
        r_ev_board_support->call_set_cp_state(*out.to_board_cp);
    }
    for (const auto& event : out.to_car_events) {
        p_board_support->publish_bsp_event(event);
    }
}

void EvDriverSim::enable(bool value) {
    auto state = m_state.handle();
    r_ev_board_support->call_enable(value);
    if (!value) {
        apply(state->faults.reapply());
        apply(state->gate.on_board_reset());
    }
}

void EvDriverSim::set_cp(types::ev_board_support::EvCpState cp) {
    auto state = m_state.handle();
    apply(state->gate.on_car_set_cp(state->faults.on_car_set_cp(cp)));
}

void EvDriverSim::diode_fail(bool value) {
    auto state = m_state.handle();
    apply(state->faults.on_car_diode_fail(value));
}

void EvDriverSim::set_rcd_error(double current_mA) {
    auto state = m_state.handle();
    apply(state->faults.on_car_set_rcd_error(current_mA));
}

void EvDriverSim::shutdown() {
    stop();
    invoke_shutdown(*p_board_support);
}

} // namespace module
