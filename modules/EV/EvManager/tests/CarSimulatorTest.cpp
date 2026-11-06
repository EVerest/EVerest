// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "../main/car_simulatorImpl.hpp"
#include "../main/constants.hpp"
#include <ModuleAdapterStub.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <functional>
#include <map>

namespace module::main {
struct CarSimulatorTestAccess {
    static void start(car_simulatorImpl& impl) {
        impl.loop_interval_ms = constants::DEFAULT_LOOP_INTERVAL_MS;
        impl.register_all_commands();
        impl.car_simulation = std::make_unique<CarSimulation>(
            impl.mod->r_ev_board_support, impl.mod->r_ev, impl.mod->r_slac, impl.mod->p_ev_manager, impl.mod->config);
        impl.subscribe_to_variables_on_init();
        impl.subscribe_to_external_mqtt();
    }
    static void tick(car_simulatorImpl& impl) {
        impl.run_simulation_step();
    }
    static void after_simulation_loop(car_simulatorImpl& impl, std::function<void()> hook) {
        impl.after_simulation_loop = std::move(hook);
    }
};
} // namespace module::main

namespace {
using types::ev_board_support::EvCpState;

struct Adapter : module::stub::QuietModuleAdapterStub {
    EvCpState cp{EvCpState::A};
    bool power_allowed{false};
    int stop_count{0};
    std::optional<EvCpState> cp_at_disable;
    std::optional<bool> power_at_disable;
    std::map<std::string, ValueCallback> subscriptions;
    std::map<std::string, StringHandler> commands;
    bool accept_start{true};
    int start_count{0};
    int cp_b_writes{0};

    Result call_fn(const Requirement&, const std::string& name, Parameters params) override {
        if (name == "set_cp_state") {
            cp = params.at("cp_state").get<EvCpState>();
            cp_b_writes += cp == EvCpState::B ? 1 : 0;
        } else if (name == "allow_power_on") {
            power_allowed = params.at("value").get<bool>();
        } else if (name == "stop_charging") {
            ++stop_count;
        } else if (name == "enable" && !params.at("value").get<bool>()) {
            cp_at_disable = cp;
            power_at_disable = power_allowed;
            // Like YetiSimulator, a disabled board drops the pilot to A.
            cp = EvCpState::A;
        } else if (name == "start_charging") {
            start_count += accept_start ? 1 : 0;
            return accept_start;
        }
        return {};
    }
    void subscribe_fn(const Requirement&, const std::string& name, ValueCallback callback) override {
        subscriptions[name] = callback;
    }
    std::function<void()> ext_mqtt_subscribe_fn(const std::string& topic, StringHandler handler) override {
        commands[topic.substr(topic.rfind('/') + 1)] = handler;
        return [] {};
    }
};

class Provider : public ev_managerImplBase {
public:
    using ev_managerImplBase::ev_managerImplBase;

private:
    void init() override {
    }
    void ready() override {
    }
};

struct Simulator {
    Adapter adapter;
    Everest::MqttProvider mqtt{adapter};
    ModuleInfo info{};
    module::Conf config{};
    module::main::Conf impl_config{};
    Everest::PtrContainer<module::EvManager> ptr;
    std::vector<std::unique_ptr<ISO15118_evIntf>> ev_interfaces() {
        std::vector<std::unique_ptr<ISO15118_evIntf>> result;
        result.push_back(std::make_unique<ISO15118_evIntf>(&adapter, Requirement{"ev", 0}, "ev", std::nullopt));
        return result;
    }
    module::EvManager mod{
        info,
        mqtt,
        {},
        std::make_unique<Provider>(&adapter, "ev_manager"),
        std::make_unique<ev_board_supportIntf>(&adapter, Requirement{"ev_board_support", 0}, "bsp", std::nullopt),
        ev_interfaces(),
        {},
        {},
        {},
        config};
    module::main::car_simulatorImpl impl{&adapter, ptr, impl_config};

    Simulator() {
        config.dc_energy_capacity = 60000;
        config.soc = 50;
        ptr.set(&mod);
        module::main::CarSimulatorTestAccess::start(impl);
        adapter.commands.at("enable")("true");
    }
    void tick() {
        module::main::CarSimulatorTestAccess::tick(impl);
    }
    void execute(const std::string& commands) {
        adapter.commands.at("execute_charging_session")(commands);
    }
    void modify(const std::string& commands) {
        adapter.commands.at("modify_charging_session")(commands);
    }
    void finish_dc_list() {
        execute("iso_start_v2g_session dc;iso_dc_power_on");
        tick();
        adapter.subscriptions.at("dc_power_on")(nullptr);
        tick();
        REQUIRE(adapter.cp == EvCpState::C);
        REQUIRE(adapter.power_allowed);
    }
};
} // namespace

TEST_CASE("Execute refuses after a DC list ends until modify stops the session", "[EvManager][list-end]") {
    Simulator sim;
    sim.finish_dc_list();
    sim.execute("iso_stop_charging");
    sim.tick();
    CHECK(sim.adapter.cp == EvCpState::C);
    CHECK(sim.adapter.power_allowed);

    SECTION("Modify delivers a stop without a running list") {
        sim.modify("iso_stop_charging");
        sim.tick();
        CHECK(sim.adapter.cp == EvCpState::B);
        CHECK_FALSE(sim.adapter.power_allowed);

        sim.modify("unplug");
        sim.tick();
        CHECK(sim.adapter.cp == EvCpState::A);
    }
    SECTION("Pilot loss unplugs without a running list") {
        sim.adapter.subscriptions.at("bsp_event")(
            types::board_support_common::BspEvent{types::board_support_common::Event::Disconnected});
        sim.tick();
        CHECK(sim.adapter.cp == EvCpState::A);
        CHECK_FALSE(sim.adapter.power_allowed);
    }
}

TEST_CASE("A rejected start leaves an earlier session to a modify stop", "[EvManager][list-end]") {
    Simulator sim;
    sim.finish_dc_list();
    sim.adapter.accept_start = false;
    sim.modify("iso_start_v2g_session ac;iso_stop_charging");
    sim.tick();
    sim.tick();
    CHECK(sim.adapter.cp == EvCpState::C);
    CHECK(sim.adapter.power_allowed);

    sim.modify("iso_stop_charging");
    sim.tick();
    CHECK(sim.adapter.cp == EvCpState::B);
    CHECK_FALSE(sim.adapter.power_allowed);
}

TEST_CASE("A list ending at B keeps its live V2G session", "[EvManager][list-end]") {
    Simulator sim;
    sim.execute("pause;iso_start_v2g_session dc");
    sim.tick();
    REQUIRE(sim.adapter.start_count == 1);
    REQUIRE(sim.adapter.cp == EvCpState::B);

    sim.execute("iso_start_v2g_session ac");
    sim.tick();
    CHECK(sim.adapter.start_count == 1);

    SECTION("Modify unplugs and ends it") {
        sim.modify("unplug");
        sim.tick();
        REQUIRE(sim.adapter.cp == EvCpState::A);
        sim.execute("iso_start_v2g_session ac");
        sim.tick();
        CHECK(sim.adapter.start_count == 2);
    }
    SECTION("The provider finishing it ends it") {
        sim.adapter.subscriptions.at("v2g_session_finished")(nullptr);
        sim.execute("iso_start_v2g_session ac");
        sim.tick();
        CHECK(sim.adapter.start_count == 2);
    }
    SECTION("Disabling the simulation keeps it") {
        sim.adapter.commands.at("enable")("false");
        sim.adapter.commands.at("enable")("true");
        sim.execute("iso_start_v2g_session ac");
        sim.tick();
        CHECK(sim.adapter.start_count == 1);
        CHECK(sim.adapter.stop_count == 0);
    }
}

TEST_CASE("Unplugging an unplugged vehicle stops its live V2G session", "[EvManager][list-end]") {
    Simulator sim;
    sim.execute("iso_start_v2g_session ac;sleep 30");
    sim.tick();
    REQUIRE(sim.adapter.start_count == 1);
    const auto stops = sim.adapter.stop_count;

    sim.modify("unplug");
    sim.tick();
    CHECK(sim.adapter.stop_count == stops + 1);
    sim.execute("iso_start_v2g_session ac");
    sim.tick();
    CHECK(sim.adapter.start_count == 2);
}

TEST_CASE("Execute refuses while an accepted V2G session waits for power", "[EvManager][replace]") {
    Simulator sim;
    sim.execute("pause;iso_start_v2g_session dc;iso_wait_pwr_ready;iso_dc_power_on");
    sim.tick();
    REQUIRE(sim.adapter.start_count == 1);
    REQUIRE(sim.adapter.cp == EvCpState::B);

    sim.execute("iso_start_v2g_session ac");
    sim.tick();
    CHECK(sim.adapter.start_count == 1);

    // The running list and its session carry on.
    sim.adapter.subscriptions.at("ev_power_ready")(true);
    sim.tick();
    CHECK(sim.adapter.cp == EvCpState::C);
    sim.adapter.subscriptions.at("dc_power_on")(nullptr);
    sim.tick();
    CHECK(sim.adapter.power_allowed);
}

TEST_CASE("Execute replaces a list parked after its V2G session stopped", "[EvManager][replace]") {
    Simulator sim;
    sim.execute("pause;iso_start_v2g_session dc;iso_wait_v2g_session_stopped;sleep 36000");
    sim.tick();
    REQUIRE(sim.adapter.start_count == 1);
    sim.adapter.subscriptions.at("v2g_session_finished")(nullptr);
    sim.tick();

    sim.execute("iso_start_v2g_session ac");
    sim.tick();
    CHECK(sim.adapter.start_count == 2);
}

TEST_CASE("Execute starts a session on an idle vehicle", "[EvManager][replace]") {
    Simulator sim;
    sim.execute("pause");
    sim.tick();
    REQUIRE(sim.adapter.cp == EvCpState::B);

    sim.execute("iso_start_v2g_session ac");
    sim.tick();
    CHECK(sim.adapter.start_count == 1);
}

TEST_CASE("Modify releases a CP C pulse it cuts short", "[EvManager][replace]") {
    Simulator sim;
    sim.execute("pause");
    sim.tick();
    sim.execute("cp_c_pulse 4");
    sim.tick();
    REQUIRE(sim.adapter.cp == EvCpState::C);

    sim.modify("sleep 30");
    CHECK(sim.adapter.cp == EvCpState::B);
}

TEST_CASE("A CP C pulse cut short on an unplugged vehicle is released to A", "[EvManager][replace]") {
    Simulator sim;
    sim.execute("sleep 30");
    sim.tick();
    sim.modify("cp_c_pulse 4");
    sim.tick();
    REQUIRE(sim.adapter.cp == EvCpState::C);

    SECTION("by modify") {
        sim.modify("sleep 30");
        CHECK(sim.adapter.cp == EvCpState::A);
    }
    SECTION("by execute") {
        sim.execute("sleep 30");
        CHECK(sim.adapter.cp == EvCpState::A);
    }
}

TEST_CASE("A CP C pulse that runs out on an unplugged vehicle ends in A", "[EvManager][replace]") {
    Simulator sim;
    sim.execute("sleep 30");
    sim.tick();
    sim.modify("cp_c_pulse 0.25;sleep 30");
    sim.tick();
    REQUIRE(sim.adapter.cp == EvCpState::C);
    sim.tick();
    CHECK(sim.adapter.cp == EvCpState::A);
}

TEST_CASE("Modify during the stop dwell does not hand it to the next stop wait", "[EvManager][replace]") {
    Simulator sim;
    sim.execute("pause;iso_start_v2g_session dc;iso_wait_for_stop 0.001");
    sim.tick();
    REQUIRE(sim.adapter.stop_count == 1);
    sim.adapter.subscriptions.at("power_delivery_stopped")(nullptr);
    sim.tick();
    REQUIRE(sim.adapter.cp == EvCpState::B);

    sim.modify("iso_wait_for_stop 0.001");
    sim.tick();
    CHECK(sim.adapter.stop_count == 2);
}

TEST_CASE("Modify after a finished CP C pulse leaves the pilot alone", "[EvManager][replace]") {
    Simulator sim;
    sim.execute("pause");
    sim.tick();
    sim.execute("cp_c_pulse 0.25;sleep 30");
    sim.tick();
    REQUIRE(sim.adapter.cp == EvCpState::C);
    sim.tick();
    REQUIRE(sim.adapter.cp == EvCpState::B);
    const auto b_writes = sim.adapter.cp_b_writes;

    sim.modify("sleep 1");
    sim.tick();
    CHECK(sim.adapter.cp_b_writes == b_writes);
}

TEST_CASE("A charger stop releases a DC vehicle after its command list ends", "[EvManager][list-end]") {
    Simulator sim;
    sim.finish_dc_list();
    sim.adapter.subscriptions.at("stop_from_charger")(nullptr);
    sim.adapter.subscriptions.at("power_delivery_stopped")(nullptr);
    sim.tick();
    REQUIRE(sim.adapter.cp == EvCpState::C);
    sim.adapter.subscriptions.at("v2g_session_finished")(nullptr);
    sim.tick();
    CHECK(sim.adapter.cp == EvCpState::B);
    CHECK_FALSE(sim.adapter.power_allowed);
    sim.execute("unplug");
    sim.tick();
    CHECK(sim.adapter.cp == EvCpState::A);
}

TEST_CASE("Disabling a charging vehicle unplugs before disabling the board", "[EvManager][disable]") {
    Simulator sim;
    sim.finish_dc_list();
    sim.adapter.commands.at("enable")("false");
    CHECK(sim.adapter.cp_at_disable == EvCpState::A);
    CHECK(sim.adapter.power_at_disable == false);
    CHECK(sim.adapter.cp == EvCpState::A);
    CHECK_FALSE(sim.adapter.power_allowed);
    CHECK(sim.adapter.stop_count == 1);

    sim.adapter.commands.at("enable")("true");
    sim.execute("iso_start_v2g_session dc;iso_dc_power_on");
    sim.tick();
    sim.adapter.subscriptions.at("dc_power_on")(nullptr);
    sim.tick();
    CHECK(sim.adapter.cp == EvCpState::C);
    CHECK(sim.adapter.power_allowed);
    CHECK(sim.adapter.stop_count == 1);
}

TEST_CASE("Disabling an active vehicle discards the running command list", "[EvManager][disable]") {
    Simulator sim;
    sim.execute("draw_power_fixed 6,1;sleep 1;error_e");
    sim.tick();
    REQUIRE(sim.adapter.cp == EvCpState::C);
    REQUIRE(sim.adapter.power_allowed);

    sim.adapter.commands.at("enable")("false");
    CHECK(sim.adapter.cp == EvCpState::A);
    CHECK_FALSE(sim.adapter.power_allowed);
    CHECK(sim.adapter.stop_count == 1);
    sim.adapter.commands.at("enable")("true");
    for (int i = 0; i < 5; ++i) {
        sim.tick();
    }
    CHECK(sim.adapter.cp == EvCpState::A);
    CHECK_FALSE(sim.adapter.power_allowed);
    CHECK(sim.adapter.stop_count == 1);
}

TEST_CASE("Disabling an idle plugged-in vehicle preserves its presence", "[EvManager][disable]") {
    Simulator sim;
    sim.execute("pause");
    sim.tick();
    REQUIRE(sim.adapter.cp == EvCpState::B);

    sim.adapter.commands.at("enable")("false");
    CHECK(sim.adapter.cp_at_disable == EvCpState::B);
    CHECK(sim.adapter.power_at_disable == false);
    CHECK(sim.adapter.stop_count == 0);
    REQUIRE(sim.adapter.cp == EvCpState::A);

    sim.adapter.commands.at("enable")("true");
    CHECK(sim.adapter.cp == EvCpState::B);
    CHECK_FALSE(sim.adapter.power_allowed);
    CHECK(sim.adapter.stop_count == 0);
}

TEST_CASE("Disabling after a pending pilot loss unplugs the vehicle once", "[EvManager][disable]") {
    Simulator sim;
    sim.finish_dc_list();
    sim.adapter.subscriptions.at("bsp_event")(
        types::board_support_common::BspEvent{types::board_support_common::Event::Disconnected});
    sim.adapter.commands.at("enable")("false");
    sim.tick();
    CHECK(sim.adapter.cp == EvCpState::A);
    CHECK(sim.adapter.stop_count == 1);
}

TEST_CASE("A charger pause resumes a DC session through power-on", "[EvManager][resume]") {
    Simulator sim;
    sim.execute("pause;iso_start_v2g_session dc;iso_wait_pwr_ready;iso_dc_power_on;iso_wait_for_stop 36000");
    sim.tick();
    sim.adapter.subscriptions.at("ev_power_ready")(true);
    sim.tick();
    sim.adapter.subscriptions.at("dc_power_on")(nullptr);
    sim.tick();
    REQUIRE(sim.adapter.power_allowed);

    sim.adapter.subscriptions.at("pause_from_charger")(nullptr);
    sim.tick();
    REQUIRE_FALSE(sim.adapter.power_allowed);
    sim.adapter.subscriptions.at("v2g_session_finished")(nullptr);
    types::board_support_common::BspMeasurement pwm{};
    pwm.cp_pwm_duty_cycle = 5;
    sim.adapter.subscriptions.at("bsp_measurement")(pwm);
    for (int i = 0; i < 20 and sim.adapter.start_count < 2; ++i) {
        sim.tick();
    }
    REQUIRE(sim.adapter.start_count == 2);

    sim.adapter.subscriptions.at("ev_power_ready")(true);
    sim.tick();
    sim.adapter.subscriptions.at("dc_power_on")(nullptr);
    sim.tick();
    CHECK(sim.adapter.cp == EvCpState::C);
    CHECK(sim.adapter.power_allowed);
}

TEST_CASE("A list executed while the previous one ends keeps running", "[EvManager][replace]") {
    Simulator sim;
    // "pause" finishes; a start the provider rejects fails.
    const bool previous_fails = GENERATE(false, true);
    sim.adapter.accept_start = not previous_fails;
    bool replaced = false;
    module::main::CarSimulatorTestAccess::after_simulation_loop(sim.impl, [&sim, &replaced] {
        if (not replaced) {
            replaced = true;
            sim.adapter.accept_start = true;
            sim.execute("iso_start_v2g_session ac");
        }
    });
    sim.execute(previous_fails ? "iso_start_v2g_session dc" : "pause");
    sim.tick();
    REQUIRE(replaced);
    REQUIRE(sim.adapter.start_count == 0);

    sim.tick();
    CHECK(sim.adapter.start_count == 1);
}

TEST_CASE("A V2G session ending before power ready fails the list once", "[EvManager][list-end]") {
    Simulator sim;
    const std::string commands{"pause;iso_start_v2g_session dc;iso_wait_pwr_ready;iso_dc_power_on;unplug"};
    sim.config.auto_exec = true;
    sim.config.auto_exec_infinite = true;
    sim.config.auto_exec_commands = commands;
    sim.execute(commands);
    sim.tick();
    REQUIRE(sim.adapter.start_count == 1);
    REQUIRE(sim.adapter.cp == EvCpState::B);

    sim.adapter.subscriptions.at("v2g_session_finished")(nullptr);
    for (int i = 0; i < 5; ++i) {
        sim.tick();
    }
    CHECK(sim.adapter.cp == EvCpState::B);
    CHECK_FALSE(sim.adapter.power_allowed);
    CHECK(sim.adapter.start_count == 1);
}

TEST_CASE("A list executed while the previous one is cancelled keeps running", "[EvManager][replace]") {
    Simulator sim;
    const bool cancelled = GENERATE(false, true);
    sim.config.auto_exec = true;
    sim.config.auto_exec_infinite = true;
    sim.config.auto_exec_commands = "iso_start_v2g_session dc";
    bool replaced = false;
    module::main::CarSimulatorTestAccess::after_simulation_loop(sim.impl, [&sim, &replaced, cancelled] {
        if (not replaced) {
            replaced = true;
            // The pilot loss belongs to the list that is about to be replaced.
            if (cancelled) {
                sim.adapter.subscriptions.at("bsp_event")(
                    types::board_support_common::BspEvent{types::board_support_common::Event::Disconnected});
            }
            sim.execute("pause;iso_start_v2g_session ac");
        }
    });
    sim.execute("pause");
    sim.tick();
    REQUIRE(replaced);
    CHECK(sim.adapter.cp == EvCpState::B);
    CHECK(sim.adapter.start_count == 0);

    sim.tick();
    CHECK(sim.adapter.cp == EvCpState::B);
    CHECK(sim.adapter.start_count == 1);
}

TEST_CASE("A cancelled list restarts under auto_exec_infinite", "[EvManager][list-end]") {
    Simulator sim;
    const std::string commands{"pause;iso_start_v2g_session ac;sleep 30"};
    sim.config.auto_exec = true;
    sim.config.auto_exec_infinite = true;
    sim.config.auto_exec_commands = commands;
    sim.execute(commands);
    sim.tick();
    REQUIRE(sim.adapter.start_count == 1);

    sim.adapter.subscriptions.at("bsp_event")(
        types::board_support_common::BspEvent{types::board_support_common::Event::Disconnected});
    sim.tick();
    CHECK(sim.adapter.cp == EvCpState::A);
    sim.tick();
    CHECK(sim.adapter.start_count == 2);
}

TEST_CASE("A pilot loss pending while the list queues its plugin commands resets the vehicle",
          "[EvManager][list-end]") {
    Simulator sim;
    sim.config.plugin_commands = "sleep 30";
    sim.execute("pause");
    sim.tick();
    REQUIRE(sim.adapter.cp == EvCpState::B);

    sim.modify("plugin");
    sim.adapter.subscriptions.at("bsp_event")(
        types::board_support_common::BspEvent{types::board_support_common::Event::Disconnected});
    sim.tick();
    CHECK(sim.adapter.cp == EvCpState::A);
    CHECK_FALSE(sim.adapter.power_allowed);
}
