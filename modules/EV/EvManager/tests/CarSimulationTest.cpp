// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "../main/car_simulation.hpp"
#include "../main/constants.hpp"
#include <ModuleAdapterStub.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <string>
#include <vector>

namespace {

struct RecordingModuleAdapter : public module::stub::QuietModuleAdapterStub {
    std::vector<std::string> calls;

    Result call_fn(const Requirement&, const std::string& cmd, Parameters) override {
        calls.push_back(cmd);
        if (cmd == "start_charging") {
            return true;
        }
        return std::nullopt;
    }
};

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
}
