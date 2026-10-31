// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "../main/car_simulation.hpp"
#include "../main/command_registry.hpp"
#include "../main/constants.hpp"
#include "../main/simulation_command.hpp"
#include <ModuleAdapterStub.hpp>
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct RecordingModuleAdapter : public module::stub::QuietModuleAdapterStub {
    std::vector<std::string> calls;

    std::optional<bool> power_allowed;
    std::optional<types::ev_board_support::EvCpState> cp_state;
    bool accept_start_charging{true};
    std::vector<Parameters> start_charging_params;
    std::vector<types::board_support_common::Event> published_bsp_events;

    void publish_fn(const std::string&, const std::string& name, Value value) override {
        if (name == "bsp_event") {
            published_bsp_events.push_back(value.get<types::board_support_common::BspEvent>().event);
        }
    }

    Result call_fn(const Requirement&, const std::string& cmd, Parameters params) override {
        calls.push_back(cmd);
        if (cmd == "set_cp_state") {
            cp_state = params.at("cp_state").get<types::ev_board_support::EvCpState>();
        }
        if (cmd == "allow_power_on") {
            power_allowed = params.at("value").get<bool>();
        }
        if (cmd == "start_charging") {
            start_charging_params.push_back(params);
            return accept_start_charging;
        }
        if (cmd == "trigger_matching") {
            return true;
        }
        return std::nullopt;
    }
};

class EvManagerProvider : public ev_managerImplBase {
public:
    using ev_managerImplBase::ev_managerImplBase;

private:
    void init() override {
    }
    void ready() override {
    }
};

// The iso_start_v2g_session entry of a command list.
std::string start_command_of(const std::string& commands) {
    std::stringstream stream{commands};
    std::string command;
    while (std::getline(stream, command, ';')) {
        if (command.rfind("iso_start_v2g_session", 0) == 0) {
            return command;
        }
    }
    return {};
}

} // namespace

SCENARIO("iso_wait_for_stop leaves the pilot in B for a dwell before it completes", "[CarSimulation]") {
    constexpr size_t loop_interval_ms = constants::DEFAULT_LOOP_INTERVAL_MS;
    const CmdArguments arguments{"36000"};
    const auto dwell_ticks = constants::STOP_DWELL_MS / loop_interval_ms;
    REQUIRE(dwell_ticks >= 1);

    RecordingModuleAdapter adapter;
    const std::unique_ptr<ev_board_supportIntf> r_ev_board_support =
        std::make_unique<ev_board_supportIntf>(&adapter, Requirement{"ev_board_support", 0}, "bsp", std::nullopt);
    std::vector<std::unique_ptr<ISO15118_evIntf>> r_ev;
    r_ev.push_back(std::make_unique<ISO15118_evIntf>(&adapter, Requirement{"ev", 0}, "ev", std::nullopt));
    const std::vector<std::unique_ptr<ev_slacIntf>> r_slac;
    const std::unique_ptr<ev_managerImplBase> p_ev_manager;
    const module::Conf config{};

    CarSimulation sim{r_ev_board_support, r_ev, r_slac, p_ev_manager, config};

    GIVEN("A stop that is holding CP C for the V2G wind-down") {
        sim.set_state(SimState::ISO_CHARGING_REGULATED);
        sim.set_v2g_session_active(true);
        sim.set_iso_stopped(true);
        REQUIRE_FALSE(sim.iso_wait_for_stop(arguments, loop_interval_ms));
        REQUIRE(sim.get_state() == SimState::ISO_CHARGING_REGULATED);

        WHEN("PowerDelivery(Stop) is confirmed while the V2G session is active") {
            sim.set_power_delivery_stopped(true);
            adapter.calls.clear();

            THEN("Power is disabled and CP drops to B for the dwell without ending V2G") {
                CHECK_FALSE(sim.iso_wait_for_stop(arguments, loop_interval_ms));
                CHECK(adapter.calls == std::vector<std::string>{"allow_power_on"});
                CHECK(adapter.power_allowed == false);
                CHECK(sim.get_state() == SimState::PLUGGED_IN);
                CHECK_FALSE(sim.iso_wait_v2g_session_stopped({}));
                for (size_t tick = 1; tick < dwell_ticks; ++tick) {
                    CHECK_FALSE(sim.iso_wait_for_stop(arguments, loop_interval_ms));
                    CHECK(sim.get_state() == SimState::PLUGGED_IN);
                    CHECK_FALSE(sim.get_modify_charging_session_cmds().has_value());
                }
                CHECK_FALSE(sim.iso_wait_for_stop(arguments, loop_interval_ms));
                CHECK(sim.get_modify_charging_session_cmds().has_value());
            }
        }

        WHEN("The V2G session reports finished") {
            sim.set_v2g_session_active(false);

            THEN("The pilot drops to B but the command keeps blocking for the dwell") {
                CHECK_FALSE(sim.iso_wait_for_stop(arguments, loop_interval_ms));
                CHECK(sim.get_state() == SimState::PLUGGED_IN);

                for (size_t tick = 1; tick < dwell_ticks; ++tick) {
                    CHECK_FALSE(sim.iso_wait_for_stop(arguments, loop_interval_ms));
                    CHECK(sim.get_state() == SimState::PLUGGED_IN);
                }
                CHECK_FALSE(sim.iso_wait_for_stop(arguments, loop_interval_ms));
                CHECK(sim.get_state() == SimState::PLUGGED_IN);
                REQUIRE(sim.get_modify_charging_session_cmds().has_value());
                CHECK(sim.get_modify_charging_session_cmds()->find("iso_start_v2g_session") != std::string::npos);
            }
        }

        WHEN("A new session starts with the dwell one tick from completing") {
            sim.set_v2g_session_active(false);
            REQUIRE_FALSE(sim.iso_wait_for_stop(arguments, loop_interval_ms));
            for (size_t tick = 1; tick < dwell_ticks; ++tick) {
                REQUIRE_FALSE(sim.iso_wait_for_stop(arguments, loop_interval_ms));
            }

            REQUIRE(sim.iso_start_v2g_session({constants::DC, "auto", "0", "0"}, false));
            sim.set_state(SimState::ISO_CHARGING_REGULATED);
            sim.set_iso_stopped(true);
            adapter.calls.clear();

            THEN("The next stop sends PowerDelivery(stop) and holds CP C") {
                CHECK_FALSE(sim.iso_wait_for_stop(arguments, loop_interval_ms));
                CHECK(adapter.calls == std::vector<std::string>{"stop_charging"});
                CHECK(sim.get_state() == SimState::ISO_CHARGING_REGULATED);
            }
        }

        WHEN("A new session starts while the stop hold is armed") {
            sim.set_power_delivery_stopped(GENERATE(false, true));
            REQUIRE(sim.iso_start_v2g_session({constants::DC, "auto", "0", "0"}, false));
            sim.set_state(SimState::ISO_CHARGING_REGULATED);
            sim.set_iso_stopped(true);
            adapter.calls.clear();

            THEN("The next stop sends PowerDelivery(stop) and holds CP C for a full budget") {
                CHECK_FALSE(sim.iso_wait_for_stop(arguments, loop_interval_ms));
                CHECK(adapter.calls == std::vector<std::string>{"stop_charging"});
                CHECK(sim.get_state() == SimState::ISO_CHARGING_REGULATED);

                const auto hold_ticks = constants::STOP_HOLD_BUDGET_MS / loop_interval_ms + 1;
                size_t ticks_until_release = 0;
                while (sim.get_state() == SimState::ISO_CHARGING_REGULATED and ticks_until_release <= hold_ticks) {
                    sim.iso_wait_for_stop(arguments, loop_interval_ms);
                    ++ticks_until_release;
                }
                CHECK(ticks_until_release == hold_ticks);
                CHECK(sim.get_state() == SimState::PLUGGED_IN);
            }
        }
    }

    GIVEN("The EV's own stop timer expires while V2G is active") {
        sim.set_state(SimState::ISO_CHARGING_REGULATED);
        sim.set_v2g_session_active(true);
        REQUIRE_FALSE(sim.iso_wait_for_stop({"0"}, loop_interval_ms));
        REQUIRE(sim.get_state() == SimState::ISO_CHARGING_REGULATED);
        sim.set_power_delivery_stopped(true);
        REQUIRE_FALSE(sim.iso_wait_for_stop({"0"}, loop_interval_ms));
        CHECK(sim.get_state() == SimState::PLUGGED_IN);
        CHECK(adapter.power_allowed == false);
        CHECK_FALSE(sim.iso_wait_v2g_session_stopped({}));
        for (size_t tick = 1; tick < dwell_ticks; ++tick) {
            CHECK_FALSE(sim.iso_wait_for_stop({"0"}, loop_interval_ms));
        }
        CHECK(sim.iso_wait_for_stop({"0"}, loop_interval_ms));
    }
}

SCENARIO("An accepted Stop releases DC but holds MCS until session end", "[CarSimulation][stop-hold]") {
    using types::ev_board_support::EvCpState;
    const auto energy_mode = GENERATE(constants::DC, constants::MCS);
    constexpr size_t loop_interval_ms = constants::DEFAULT_LOOP_INTERVAL_MS;
    const CmdArguments arguments{"36000"};
    RecordingModuleAdapter adapter;
    const std::unique_ptr<ev_board_supportIntf> r_ev_board_support =
        std::make_unique<ev_board_supportIntf>(&adapter, Requirement{"ev_board_support", 0}, "bsp", std::nullopt);
    std::vector<std::unique_ptr<ISO15118_evIntf>> r_ev;
    r_ev.push_back(std::make_unique<ISO15118_evIntf>(&adapter, Requirement{"ev", 0}, "ev", std::nullopt));
    const std::vector<std::unique_ptr<ev_slacIntf>> r_slac;
    const std::unique_ptr<ev_managerImplBase> p_ev_manager =
        std::make_unique<EvManagerProvider>(&adapter, "ev_manager");
    const module::Conf config{};
    CarSimulation sim{r_ev_board_support, r_ev, r_slac, p_ev_manager, config};

    REQUIRE(sim.iso_start_v2g_session({energy_mode, "auto", "0", "0"}, false));
    sim.set_state(SimState::ISO_CHARGING_REGULATED);
    sim.state_machine();
    REQUIRE(adapter.cp_state == EvCpState::C);
    sim.set_iso_stopped(true);
    REQUIRE_FALSE(sim.iso_wait_for_stop(arguments, loop_interval_ms));
    sim.set_power_delivery_stopped(true);
    REQUIRE_FALSE(sim.iso_wait_for_stop(arguments, loop_interval_ms));
    sim.state_machine();

    if (energy_mode == constants::DC) {
        CHECK(adapter.cp_state == EvCpState::B);
        CHECK(adapter.power_allowed == false);
        CHECK_FALSE(sim.iso_wait_v2g_session_stopped({}));
    } else {
        REQUIRE(adapter.cp_state == EvCpState::C);
        REQUIRE(sim.get_state() == SimState::ISO_CHARGING_REGULATED);
        SECTION("Session end releases the hold") {
            sim.set_v2g_session_active(false);
            REQUIRE_FALSE(sim.iso_wait_for_stop(arguments, loop_interval_ms));
        }
        SECTION("The hold budget still expires after the Stop signal") {
            const auto remaining_ticks = constants::STOP_HOLD_BUDGET_MS / loop_interval_ms;
            for (size_t tick = 1; tick < remaining_ticks; ++tick) {
                REQUIRE_FALSE(sim.iso_wait_for_stop(arguments, loop_interval_ms));
                REQUIRE(sim.get_state() == SimState::ISO_CHARGING_REGULATED);
            }
            REQUIRE_FALSE(sim.iso_wait_for_stop(arguments, loop_interval_ms));
        }
        sim.state_machine();
        CHECK(adapter.cp_state == EvCpState::B);
        CHECK(adapter.power_allowed == false);
    }
}

SCENARIO("A V2G session start the ISO15118_ev provider rejects ends the command sequence", "[CarSimulation]") {
    RecordingModuleAdapter adapter;
    const std::unique_ptr<ev_board_supportIntf> r_ev_board_support =
        std::make_unique<ev_board_supportIntf>(&adapter, Requirement{"ev_board_support", 0}, "bsp", std::nullopt);
    std::vector<std::unique_ptr<ISO15118_evIntf>> r_ev;
    r_ev.push_back(std::make_unique<ISO15118_evIntf>(&adapter, Requirement{"ev", 0}, "ev", std::nullopt));
    const std::vector<std::unique_ptr<ev_slacIntf>> r_slac;
    const std::unique_ptr<ev_managerImplBase> p_ev_manager;
    const module::Conf config{};

    CarSimulation sim{r_ev_board_support, r_ev, r_slac, p_ev_manager, config};
    sim.set_state(SimState::PLUGGED_IN);

    const auto start_charging_calls = [&adapter] {
        return std::count(adapter.calls.begin(), adapter.calls.end(), "start_charging");
    };

    GIVEN("No V2G session is running") {
        adapter.accept_start_charging = false;

        THEN("The start fails terminally after asking the provider once, and no session is marked active") {
            CHECK_THROWS_AS(sim.iso_start_v2g_session({constants::DC, "auto", "0", "0"}, false),
                            TerminalCommandFailure);
            CHECK(start_charging_calls() == 1);
            CHECK(sim.iso_wait_v2g_session_stopped({}));
        }
    }

    GIVEN("An accepted DC session that has reached power readiness") {
        REQUIRE(sim.iso_start_v2g_session({constants::DC, "auto", "0", "0"}, false));
        sim.set_iso_pwr_ready(true);
        REQUIRE(sim.iso_wait_pwr_ready({}));
        adapter.accept_start_charging = false;

        WHEN("A second, AC start is rejected while that session is active") {
            CHECK_THROWS_AS(sim.iso_start_v2g_session({constants::AC, "auto", "0", "0"}, false),
                            TerminalCommandFailure);

            THEN("The accepted session keeps its bookkeeping") {
                CHECK(start_charging_calls() == 2);
                CHECK_FALSE(sim.iso_wait_v2g_session_stopped({}));
                CHECK(sim.iso_wait_pwr_ready({}));

                // A charger stop restarts the accepted session's start, not the rejected one's.
                sim.set_iso_stopped(true);
                CHECK_FALSE(sim.iso_dc_power_on({}));
                REQUIRE(sim.get_modify_charging_session_cmds().has_value());
                CHECK(start_command_of(*sim.get_modify_charging_session_cmds()) == "iso_start_v2g_session dc,auto,0,0");
            }
        }
    }
}

SCENARIO("A V2G session that ends before power readiness fails the power-ready wait", "[CarSimulation]") {
    RecordingModuleAdapter adapter;
    const std::unique_ptr<ev_board_supportIntf> r_ev_board_support =
        std::make_unique<ev_board_supportIntf>(&adapter, Requirement{"ev_board_support", 0}, "bsp", std::nullopt);
    std::vector<std::unique_ptr<ISO15118_evIntf>> r_ev;
    r_ev.push_back(std::make_unique<ISO15118_evIntf>(&adapter, Requirement{"ev", 0}, "ev", std::nullopt));
    const std::vector<std::unique_ptr<ev_slacIntf>> r_slac;
    const std::unique_ptr<ev_managerImplBase> p_ev_manager;
    const module::Conf config{};

    CarSimulation sim{r_ev_board_support, r_ev, r_slac, p_ev_manager, config};
    sim.set_state(SimState::PLUGGED_IN);
    REQUIRE(sim.iso_start_v2g_session({constants::DC, "auto", "0", "0"}, false));
    REQUIRE_FALSE(sim.iso_wait_pwr_ready({}));

    WHEN("The provider reports the session finished") {
        sim.set_v2g_session_active(false);

        THEN("The wait fails terminally") {
            CHECK_THROWS_AS(sim.iso_wait_pwr_ready({}), TerminalCommandFailure);
        }
    }

    WHEN("The charger paused the session") {
        sim.set_iso_charger_paused(true);
        sim.set_v2g_session_active(false);

        THEN("The wait keeps waiting") {
            CHECK_FALSE(sim.iso_wait_pwr_ready({}));
        }
    }
}

SCENARIO("A charger pause or stop restarts the V2G start the provider accepted", "[CarSimulation]") {
    constexpr size_t loop_interval_ms = constants::DEFAULT_LOOP_INTERVAL_MS;
    const CmdArguments wait_arguments{"36000"};

    RecordingModuleAdapter adapter;
    const std::unique_ptr<ev_board_supportIntf> r_ev_board_support =
        std::make_unique<ev_board_supportIntf>(&adapter, Requirement{"ev_board_support", 0}, "bsp", std::nullopt);
    std::vector<std::unique_ptr<ISO15118_evIntf>> r_ev;
    r_ev.push_back(std::make_unique<ISO15118_evIntf>(&adapter, Requirement{"ev", 0}, "ev", std::nullopt));
    const std::vector<std::unique_ptr<ev_slacIntf>> r_slac;
    const std::unique_ptr<ev_managerImplBase> p_ev_manager;
    const module::Conf config{};

    CarSimulation sim{r_ev_board_support, r_ev, r_slac, p_ev_manager, config};
    sim.set_state(SimState::PLUGGED_IN);

    // Every iso_start_v2g_session arity resolves its omitted arguments and calls the four-argument form.
    CommandRegistry registry;
    registry.register_command("iso_start_v2g_session", 4, [&sim](const CmdArguments& arguments) {
        return sim.iso_start_v2g_session(arguments, false);
    });

    const auto run_restart_start = [&sim, &registry]() {
        auto& restart = sim.get_modify_charging_session_cmds();
        REQUIRE(restart.has_value());
        auto commands = SimulationCommand::parse_sim_commands(start_command_of(*restart), registry);
        REQUIRE(commands.size() == 1);
        CHECK(commands.front().execute());
    };

    const auto stop_from_charger = [&] {
        sim.set_iso_stopped(true);
        REQUIRE_FALSE(sim.iso_wait_for_stop(wait_arguments, loop_interval_ms));
        sim.set_power_delivery_stopped(true);
        sim.set_v2g_session_active(false);
        for (size_t tick = 0; tick <= constants::STOP_DWELL_MS / loop_interval_ms and
                              not sim.get_modify_charging_session_cmds().has_value();
             ++tick) {
            REQUIRE_FALSE(sim.iso_wait_for_stop(wait_arguments, loop_interval_ms));
        }
    };

    GIVEN("An accepted session started with explicit payment, departure time and energy amount") {
        const std::string mode = GENERATE(constants::AC, constants::DC, constants::MCS, constants::AC_BPT,
                                          constants::AC_DER, constants::DC_BPT);
        REQUIRE(sim.iso_start_v2g_session({mode, "contract", "7200", "42"}, false));
        sim.set_state(SimState::ISO_CHARGING_REGULATED);
        REQUIRE(adapter.start_charging_params.size() == 1);
        const auto accepted = adapter.start_charging_params.front();
        const auto expected_start = "iso_start_v2g_session " + mode + ",contract,7200,42";

        WHEN("The charger pauses the session") {
            sim.set_iso_charger_paused(true);
            REQUIRE_FALSE(sim.iso_wait_for_stop(wait_arguments, loop_interval_ms));

            THEN("The resume starts the same service with the same arguments") {
                REQUIRE(sim.get_modify_charging_session_cmds().has_value());
                CHECK(start_command_of(*sim.get_modify_charging_session_cmds()) == expected_start);
                run_restart_start();
                REQUIRE(adapter.start_charging_params.size() == 2);
                CHECK(adapter.start_charging_params.back() == accepted);
            }
        }

        WHEN("The charger stops the session") {
            stop_from_charger();

            THEN("The new session starts the same service with the same arguments") {
                REQUIRE(sim.get_modify_charging_session_cmds().has_value());
                CHECK(start_command_of(*sim.get_modify_charging_session_cmds()) == expected_start);
                run_restart_start();
                REQUIRE(adapter.start_charging_params.size() == 2);
                CHECK(adapter.start_charging_params.back() == accepted);
            }
        }
    }

    GIVEN("An accepted DC session with resolved default arguments") {
        REQUIRE(sim.iso_start_v2g_session({constants::DC, "auto", "0", "0"}, false));
        sim.set_state(SimState::ISO_CHARGING_REGULATED);

        WHEN("A later MCS start is accepted and the charger stops that session") {
            REQUIRE(sim.iso_start_v2g_session({constants::MCS, "externalpayment", "3600", "80"}, false));
            const auto accepted = adapter.start_charging_params.back();
            sim.set_iso_stopped(true);
            REQUIRE_FALSE(sim.iso_dc_power_on({}));

            THEN("The restart starts the later session") {
                REQUIRE(sim.get_modify_charging_session_cmds().has_value());
                CHECK(start_command_of(*sim.get_modify_charging_session_cmds()) ==
                      "iso_start_v2g_session mcs,externalpayment,3600,80");
                run_restart_start();
                CHECK(adapter.start_charging_params.back() == accepted);
            }
        }
    }
}

SCENARIO("A resumed or restarted AC session draws power again", "[CarSimulation]") {
    constexpr size_t loop_interval_ms = constants::DEFAULT_LOOP_INTERVAL_MS;
    const CmdArguments wait_arguments{"36000"};

    RecordingModuleAdapter adapter;
    const std::unique_ptr<ev_board_supportIntf> r_ev_board_support =
        std::make_unique<ev_board_supportIntf>(&adapter, Requirement{"ev_board_support", 0}, "bsp", std::nullopt);
    std::vector<std::unique_ptr<ISO15118_evIntf>> r_ev;
    r_ev.push_back(std::make_unique<ISO15118_evIntf>(&adapter, Requirement{"ev", 0}, "ev", std::nullopt));
    const std::vector<std::unique_ptr<ev_slacIntf>> r_slac;
    const std::unique_ptr<ev_managerImplBase> p_ev_manager;
    module::Conf config{};
    config.max_current = 16;

    CarSimulation sim{r_ev_board_support, r_ev, r_slac, p_ev_manager, config};
    sim.set_state(SimState::PLUGGED_IN);

    // The iso_draw_power_regulated entry of the armed resume or restart.
    const auto draw_command = [&sim]() -> std::string {
        auto& commands = sim.get_modify_charging_session_cmds();
        REQUIRE(commands.has_value());
        std::stringstream stream{*commands};
        std::string command;
        while (std::getline(stream, command, ';')) {
            if (command.rfind("iso_draw_power_regulated", 0) == 0) {
                return command;
            }
        }
        return {};
    };

    GIVEN("An AC session that never ran iso_draw_power_regulated") {
        REQUIRE(sim.iso_start_v2g_session({constants::AC, "auto", "0", "0"}, false));
        sim.set_state(SimState::ISO_CHARGING_REGULATED);

        WHEN("The charger pauses the session") {
            sim.set_iso_charger_paused(true);
            REQUIRE_FALSE(sim.iso_wait_for_stop(wait_arguments, loop_interval_ms));

            THEN("The resume draws the configured maximum current") {
                CHECK(draw_command() == "iso_draw_power_regulated 16,1");
            }
        }

        WHEN("The charger stops the session") {
            sim.set_iso_stopped(true);
            REQUIRE_FALSE(sim.iso_dc_power_on({}));

            THEN("The restart draws the configured maximum current") {
                CHECK(draw_command() == "iso_draw_power_regulated 16,1");
            }
        }
    }

    GIVEN("An AC session that never drew power after an earlier session drew 10 A") {
        REQUIRE(sim.iso_start_v2g_session({constants::AC, "auto", "0", "0"}, false));
        REQUIRE(sim.iso_draw_power_regulated({"10", "1"}));
        sim.set_v2g_session_active(false);
        sim.set_state(SimState::PLUGGED_IN);
        REQUIRE(sim.iso_start_v2g_session({constants::AC, "auto", "0", "0"}, false));
        sim.set_state(SimState::ISO_CHARGING_REGULATED);

        WHEN("The charger stops the session") {
            sim.set_iso_stopped(true);
            REQUIRE_FALSE(sim.iso_dc_power_on({}));

            THEN("The restart draws the configured maximum current") {
                CHECK(draw_command() == "iso_draw_power_regulated 16,1");
            }
        }
    }

    GIVEN("A three-phase vehicle starting an AC session in any AC energy mode") {
        const std::string mode = GENERATE(constants::AC, constants::AC_BPT, constants::AC_DER);
        REQUIRE(sim.iso_start_v2g_session({mode, "auto", "0", "0"}, true));
        sim.set_state(SimState::ISO_CHARGING_REGULATED);

        WHEN("The charger stops the session") {
            sim.set_iso_stopped(true);
            REQUIRE_FALSE(sim.iso_dc_power_on({}));

            THEN("The restart draws on three phases") {
                const auto draw = draw_command();
                CHECK(draw.substr(draw.rfind(',') + 1) == "3");
            }
        }
    }
}

SCENARIO("An unknown energy mode fails the V2G session start", "[CarSimulation]") {
    RecordingModuleAdapter adapter;
    const std::unique_ptr<ev_board_supportIntf> r_ev_board_support =
        std::make_unique<ev_board_supportIntf>(&adapter, Requirement{"ev_board_support", 0}, "bsp", std::nullopt);
    std::vector<std::unique_ptr<ISO15118_evIntf>> r_ev;
    r_ev.push_back(std::make_unique<ISO15118_evIntf>(&adapter, Requirement{"ev", 0}, "ev", std::nullopt));
    const std::vector<std::unique_ptr<ev_slacIntf>> r_slac;
    const std::unique_ptr<ev_managerImplBase> p_ev_manager;
    const module::Conf config{};

    CarSimulation sim{r_ev_board_support, r_ev, r_slac, p_ev_manager, config};
    sim.set_state(SimState::PLUGGED_IN);

    CHECK_THROWS_WITH(sim.iso_start_v2g_session({"ac_dc", "auto", "0", "0"}, false),
                      Catch::Matchers::ContainsSubstring("'ac_dc'") and
                          Catch::Matchers::ContainsSubstring("ac, ac_bpt, ac_der, dc, dc_bpt, mcs"));
    CHECK_THROWS_AS(sim.iso_start_v2g_session({"ac_dc", "auto", "0", "0"}, false), TerminalCommandFailure);
    CHECK(adapter.start_charging_params.empty());
    CHECK(sim.iso_wait_v2g_session_stopped({}));
}

SCENARIO("Relay feedback reaches ev_manager subscribers without becoming the measured pilot",
         "[CarSimulation][bsp-event]") {
    using types::board_support_common::Event;
    RecordingModuleAdapter adapter;
    const std::unique_ptr<ev_board_supportIntf> r_ev_board_support =
        std::make_unique<ev_board_supportIntf>(&adapter, Requirement{"ev_board_support", 0}, "bsp", std::nullopt);
    const std::vector<std::unique_ptr<ISO15118_evIntf>> r_ev;
    std::vector<std::unique_ptr<ev_slacIntf>> r_slac;
    r_slac.push_back(std::make_unique<ev_slacIntf>(&adapter, Requirement{"slac", 0}, "slac", std::nullopt));
    const std::unique_ptr<ev_managerImplBase> p_ev_manager =
        std::make_unique<EvManagerProvider>(&adapter, "ev_manager");
    const module::Conf config{};

    CarSimulation sim{r_ev_board_support, r_ev, r_slac, p_ev_manager, config};

    GIVEN("The pilot measures B") {
        REQUIRE_FALSE(sim.handle_bsp_event({Event::B}));
        const auto relay = GENERATE(Event::PowerOn, Event::PowerOff);

        WHEN("The vehicle BSP reports relay feedback") {
            adapter.published_bsp_events.clear();
            CHECK_FALSE(sim.handle_bsp_event({relay}));

            THEN("Subscribers get the event once, and SLAC matching still starts on the B measurement") {
                CHECK(adapter.published_bsp_events == std::vector<Event>{relay});
                CHECK(sim.get_bsp_event() == Event::B);
                CHECK(sim.cp_state_allows_matching());
                adapter.calls.clear();
                CHECK_FALSE(sim.iso_wait_slac_matched({}));
                CHECK(adapter.calls == std::vector<std::string>{"reset", "trigger_matching"});
            }
        }
    }
}

SCENARIO("Pilot loss cancels the session unless the vehicle applied it itself", "[CarSimulation][bsp-event]") {
    using types::board_support_common::Event;
    RecordingModuleAdapter adapter;
    const std::unique_ptr<ev_board_supportIntf> r_ev_board_support =
        std::make_unique<ev_board_supportIntf>(&adapter, Requirement{"ev_board_support", 0}, "bsp", std::nullopt);
    std::vector<std::unique_ptr<ISO15118_evIntf>> r_ev;
    r_ev.push_back(std::make_unique<ISO15118_evIntf>(&adapter, Requirement{"ev", 0}, "ev", std::nullopt));
    const std::vector<std::unique_ptr<ev_slacIntf>> r_slac;
    const std::unique_ptr<ev_managerImplBase> p_ev_manager =
        std::make_unique<EvManagerProvider>(&adapter, "ev_manager");
    const module::Conf config{};

    CarSimulation sim{r_ev_board_support, r_ev, r_slac, p_ev_manager, config};

    const auto aborts = [&adapter] { return std::count(adapter.calls.begin(), adapter.calls.end(), "abort_charging"); };

    GIVEN("A plugged-in vehicle whose pilot measures B") {
        sim.set_state(SimState::PLUGGED_IN);
        REQUIRE_FALSE(sim.handle_bsp_event({Event::B}));

        THEN("E, F and a plug-out end the session and abort V2G once") {
            const auto pilot_loss = GENERATE(Event::E, Event::F, Event::Disconnected);
            CHECK(sim.handle_bsp_event({pilot_loss}));
            CHECK(aborts() == 1);
        }

        THEN("Relay feedback does not end the session") {
            const auto relay = GENERATE(Event::PowerOn, Event::PowerOff);
            CHECK_FALSE(sim.handle_bsp_event({relay}));
            CHECK(aborts() == 0);
        }
    }

    GIVEN("A vehicle that applies E itself") {
        sim.set_state(SimState::ERROR_E);

        THEN("Measuring E does not end the session") {
            CHECK_FALSE(sim.handle_bsp_event({Event::E}));
            CHECK(aborts() == 0);
        }
    }
}

SCENARIO("A finished DC command list preserves the charging session", "[CarSimulation][list-end]") {
    using types::ev_board_support::EvCpState;
    RecordingModuleAdapter adapter;
    const std::unique_ptr<ev_board_supportIntf> r_ev_board_support =
        std::make_unique<ev_board_supportIntf>(&adapter, Requirement{"ev_board_support", 0}, "bsp", std::nullopt);
    std::vector<std::unique_ptr<ISO15118_evIntf>> r_ev;
    r_ev.push_back(std::make_unique<ISO15118_evIntf>(&adapter, Requirement{"ev", 0}, "ev", std::nullopt));
    const std::vector<std::unique_ptr<ev_slacIntf>> r_slac;
    const std::unique_ptr<ev_managerImplBase> p_ev_manager =
        std::make_unique<EvManagerProvider>(&adapter, "ev_manager");
    const module::Conf config{};
    CarSimulation sim{r_ev_board_support, r_ev, r_slac, p_ev_manager, config};

    REQUIRE(sim.iso_start_v2g_session({constants::DC, "contract", "7200", "42"}, false));
    sim.set_dc_power_on(true);
    REQUIRE(sim.iso_dc_power_on({}));
    sim.state_machine();
    REQUIRE(adapter.cp_state == EvCpState::C);
    REQUIRE(adapter.power_allowed == true);

    sim.reset();

    CHECK(sim.get_state() == SimState::ISO_CHARGING_REGULATED);
    CHECK_FALSE(sim.is_idle_on_the_wire());
    CHECK_FALSE(sim.iso_wait_v2g_session_stopped({}));
    CHECK(adapter.cp_state == EvCpState::C);
    CHECK(adapter.power_allowed == true);

    SECTION("A subsequent stop command releases power and the pilot") {
        REQUIRE(sim.iso_stop_charging({}));
        sim.state_machine();
        CHECK(adapter.cp_state == EvCpState::B);
        CHECK(adapter.power_allowed == false);
    }
    SECTION("A stop wait retains the active session until PowerDelivery Stop") {
        REQUIRE_FALSE(sim.iso_wait_for_stop({"0"}, constants::DEFAULT_LOOP_INTERVAL_MS));
        sim.state_machine();
        CHECK(adapter.cp_state == EvCpState::C);
        CHECK(adapter.power_allowed == true);
        sim.set_power_delivery_stopped(true);
        REQUIRE_FALSE(sim.iso_wait_for_stop({"0"}, constants::DEFAULT_LOOP_INTERVAL_MS));
        sim.state_machine();
        CHECK(adapter.cp_state == EvCpState::B);
        CHECK(adapter.power_allowed == false);
    }
    SECTION("A charger stop retains the accepted start for a later restart") {
        sim.set_iso_stopped(true);
        REQUIRE_FALSE(sim.iso_dc_power_on({}));
        REQUIRE(sim.get_modify_charging_session_cmds().has_value());
        CHECK(start_command_of(*sim.get_modify_charging_session_cmds()) == "iso_start_v2g_session dc,contract,7200,42");
    }
    SECTION("Session end releases power and the pilot") {
        sim.set_v2g_session_active(false);
        sim.end_charging_session();
        sim.state_machine();
        CHECK(adapter.cp_state == EvCpState::B);
        CHECK(adapter.power_allowed == false);
    }
    SECTION("Unplug releases power and the pilot") {
        sim.unplug_vehicle();
        sim.state_machine();
        CHECK(adapter.cp_state == EvCpState::A);
        CHECK(adapter.power_allowed == false);
    }
}

SCENARIO("Reset keeps the live V2G session of a plugged-in vehicle", "[CarSimulation][list-end]") {
    RecordingModuleAdapter adapter;
    const std::unique_ptr<ev_board_supportIntf> r_ev_board_support =
        std::make_unique<ev_board_supportIntf>(&adapter, Requirement{"ev_board_support", 0}, "bsp", std::nullopt);
    std::vector<std::unique_ptr<ISO15118_evIntf>> r_ev;
    r_ev.push_back(std::make_unique<ISO15118_evIntf>(&adapter, Requirement{"ev", 0}, "ev", std::nullopt));
    const std::vector<std::unique_ptr<ev_slacIntf>> r_slac;
    const std::unique_ptr<ev_managerImplBase> p_ev_manager =
        std::make_unique<EvManagerProvider>(&adapter, "ev_manager");
    const module::Conf config{};
    CarSimulation sim{r_ev_board_support, r_ev, r_slac, p_ev_manager, config};

    REQUIRE(sim.pause({}));
    REQUIRE(sim.iso_start_v2g_session({constants::DC, "contract", "7200", "42"}, false));
    sim.set_iso_pwr_ready(true);
    sim.set_iso_stopped(true);
    sim.set_slac_state(types::slac::State::MATCHED);

    sim.reset();

    CHECK(sim.get_state() == SimState::PLUGGED_IN);
    CHECK(sim.has_live_v2g_session());
    CHECK_FALSE(sim.iso_wait_v2g_session_stopped({}));

    SECTION("The power-ready feedback survives") {
        CHECK(sim.iso_wait_pwr_ready({}));
    }
    SECTION("The matched link survives") {
        CHECK(sim.get_slac_state() == types::slac::State::MATCHED);
        CHECK(sim.iso_wait_slac_matched({}));
    }
    SECTION("A charger stop restarts the accepted start") {
        REQUIRE_FALSE(sim.iso_dc_power_on({}));
        REQUIRE(sim.get_modify_charging_session_cmds().has_value());
        CHECK(start_command_of(*sim.get_modify_charging_session_cmds()) == "iso_start_v2g_session dc,contract,7200,42");
    }
    SECTION("Unplugging forgets it") {
        sim.unplug_vehicle();
        CHECK_FALSE(sim.has_live_v2g_session());
        CHECK(sim.iso_wait_v2g_session_stopped({}));
        CHECK_THROWS_AS(sim.iso_wait_pwr_ready({}), TerminalCommandFailure);
        CHECK(sim.get_slac_state() == types::slac::State::UNMATCHED);
    }
    SECTION("Session end lets the next reset forget it") {
        sim.set_v2g_session_active(false);
        sim.reset();
        CHECK_THROWS_AS(sim.iso_wait_pwr_ready({}), TerminalCommandFailure);
    }
}
