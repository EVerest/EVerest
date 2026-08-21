// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "../main/simulation_data.hpp"
#include <catch2/catch_test_macros.hpp>

SCENARIO("SimulationData reset keeps plug state consistent with the physical CP line", "[SimulationData]") {
    GIVEN("A simulation that finished mid-charging") {
        auto sim_data = SimulationData();
        sim_data.state = SimState::CHARGING_REGULATED;
        sim_data.last_state = SimState::CHARGING_REGULATED;
        sim_data.v2g_finished = true;
        sim_data.v2g_session_active = true;

        WHEN("reset preserves the plug state (simulation-queue completion)") {
            sim_data.reset(ResetBehavior::KeepPlugState);

            THEN("the CP/plug state stays as it was on the frozen physical line") {
                CHECK(sim_data.state == SimState::CHARGING_REGULATED);
                CHECK(sim_data.last_state == SimState::CHARGING_REGULATED);
            }
            THEN("per-session flags are cleared back to defaults") {
                CHECK(sim_data.v2g_finished == false);
                CHECK(sim_data.v2g_session_active == false);
            }
        }

        WHEN("reset does not preserve the plug state (enable / execute)") {
            sim_data.reset(ResetBehavior::Full);

            THEN("the state reverts to the UNPLUGGED default") {
                CHECK(sim_data.state == SimState::UNPLUGGED);
                CHECK(sim_data.last_state == SimState::UNDEFINED);
            }
        }
    }
}

SCENARIO("iso_wait_v2g_session_stopped does not block the queue in IEC sessions", "[SimulationData]") {
    GIVEN("No V2G session was ever started (IEC session)") {
        auto sim_data = SimulationData();
        REQUIRE(sim_data.v2g_session_active == false);

        WHEN("the wait step is advanced") {
            const auto done = sim_data.advance_wait_v2g_session_stopped();

            THEN("it completes immediately instead of blocking forever") {
                CHECK(done == true);
            }
        }
    }

    GIVEN("A V2G session is active but not yet stopped") {
        auto sim_data = SimulationData();
        sim_data.v2g_session_active = true;
        sim_data.v2g_finished = false;

        WHEN("the wait step is advanced") {
            const auto done = sim_data.advance_wait_v2g_session_stopped();

            THEN("it blocks until the session stops") {
                CHECK(done == false);
            }
        }
    }

    GIVEN("A V2G session is active and has stopped") {
        auto sim_data = SimulationData();
        sim_data.v2g_session_active = true;
        sim_data.v2g_finished = true;

        WHEN("the wait step is advanced") {
            const auto done = sim_data.advance_wait_v2g_session_stopped();

            THEN("it completes and clears the session flags") {
                CHECK(done == true);
                CHECK(sim_data.v2g_finished == false);
                CHECK(sim_data.v2g_session_active == false);
            }
        }
    }
}
