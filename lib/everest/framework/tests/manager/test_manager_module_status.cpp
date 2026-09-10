// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <catch2/catch_all.hpp>

#include <manager_module_status.hpp>

#include <vector>

namespace {

/// \brief Every ManagerState, so the tests below can assert properties over the whole enum.
/// A newly added enumerator has to be listed here; the exhaustive switch in
/// module_status_action_for() is what forces it to be considered in the first place.
const std::vector<ManagerState> ALL_STATES{
    ManagerState::Initializing,     ManagerState::StartingModules,         ManagerState::Running,
    ManagerState::RestartRequested, ManagerState::CrashShutdownInProgress, ManagerState::ShutdownRequested,
    ManagerState::ForceTerminating, ManagerState::ShutdownFinalizing,      ManagerState::Idle,
    ManagerState::Exiting};

/// \brief Mirrors Manager::is_in_shutdown_flow_state_unlocked().
bool is_shutdown_flow_state(ManagerState state) {
    return state == ManagerState::ShutdownRequested or state == ManagerState::CrashShutdownInProgress or
           state == ManagerState::ForceTerminating or state == ManagerState::RestartRequested or
           state == ManagerState::ShutdownFinalizing;
}

} // namespace

TEST_CASE("Manager module status mapping", "[manager_module_status]") {

    SECTION("every state maps to its expected action") {
        const std::vector<std::pair<ManagerState, ModuleStatusAction>> expected{
            {ManagerState::Initializing, ModuleStatusAction::AtRest},
            {ManagerState::StartingModules, ModuleStatusAction::Starting},
            {ManagerState::Running, ModuleStatusAction::Running},
            {ManagerState::RestartRequested, ModuleStatusAction::RestartTriggered},
            {ManagerState::CrashShutdownInProgress, ModuleStatusAction::Stopping},
            {ManagerState::ShutdownRequested, ModuleStatusAction::Stopping},
            {ManagerState::ForceTerminating, ModuleStatusAction::Stopping},
            {ManagerState::ShutdownFinalizing, ModuleStatusAction::Stopped},
            {ManagerState::Idle, ModuleStatusAction::AtRest},
            {ManagerState::Exiting, ModuleStatusAction::AtRest},
        };

        // Guards against a state being added to the mapping but forgotten here.
        REQUIRE(expected.size() == ALL_STATES.size());

        for (const auto& [state, action] : expected) {
            CHECK(module_status_action_for(state) == action);
        }
    }

    SECTION("only StartingModules reports Starting") {
        // Regression guard: a drain that began in StartingModules used to keep reporting Starting for
        // its whole duration, because the transition out of it matched no branch of the old handler.
        for (const auto state : ALL_STATES) {
            INFO("state index " << static_cast<int>(state));
            if (state != ManagerState::StartingModules) {
                CHECK(module_status_action_for(state) != ModuleStatusAction::Starting);
            }
        }
    }

    SECTION("no shutdown-flow state reports Starting or Running") {
        for (const auto state : ALL_STATES) {
            if (not is_shutdown_flow_state(state)) {
                continue;
            }
            INFO("state index " << static_cast<int>(state));
            const auto action = module_status_action_for(state);
            CHECK(action != ModuleStatusAction::Starting);
            CHECK(action != ModuleStatusAction::Running);
        }
    }

    SECTION("resting states map to AtRest, never plain Stopped") {
        // AtRest preserves a recorded FailedToStart while Stopped would clobber it, which is what
        // made a failed config reload unrecoverable: the manager sat in Idle while the config service
        // refused every operation that could have fixed it.
        for (const auto state : {ManagerState::Idle, ManagerState::Exiting, ManagerState::Initializing}) {
            INFO("state index " << static_cast<int>(state));
            CHECK(module_status_action_for(state) == ModuleStatusAction::AtRest);
        }
    }

    SECTION("ShutdownFinalizing reports Stopped so the reload gate is open during finalization") {
        // The restart path reloads the configuration while in ShutdownFinalizing, and
        // ConfigServiceCore only reloads when the modules are at rest.
        CHECK(module_status_action_for(ManagerState::ShutdownFinalizing) == ModuleStatusAction::Stopped);
    }
}

TEST_CASE("Manager module status transition dedup", "[manager_module_status]") {

    SECTION("a transition between states sharing an action reports nothing") {
        // Regression guard: a stop request escalated ShutdownRequested -> ForceTerminating (always,
        // without --graceful-shutdown), and a crash went through CrashShutdownInProgress as well -
        // each hop repeated "Stopping" to the client, so one stop request published it two or three
        // times. Same phase, nothing new to report.
        const std::vector<std::pair<ManagerState, ManagerState>> repeats{
            {ManagerState::ShutdownRequested, ManagerState::ForceTerminating},
            {ManagerState::ShutdownRequested, ManagerState::CrashShutdownInProgress},
            {ManagerState::CrashShutdownInProgress, ManagerState::ForceTerminating},
        };
        for (const auto& [from, to] : repeats) {
            INFO("state indices " << static_cast<int>(from) << " -> " << static_cast<int>(to));
            CHECK(module_status_action_for_transition(from, to) == std::nullopt);
        }
    }

    SECTION("exhaustive: a transition reports iff the destination's action differs") {
        for (const auto from : ALL_STATES) {
            for (const auto to : ALL_STATES) {
                INFO("state indices " << static_cast<int>(from) << " -> " << static_cast<int>(to));
                const auto action = module_status_action_for_transition(from, to);
                if (module_status_action_for(from) == module_status_action_for(to)) {
                    CHECK(action == std::nullopt);
                } else {
                    REQUIRE(action.has_value());
                    // Still derived from the destination alone (see module_status_action_for()).
                    CHECK(action.value() == module_status_action_for(to));
                }
            }
        }
    }

    SECTION("the phase changes every shutdown flow relies on still report") {
        // The stop flow's status story: request accepted -> Stopping, all pids reaped -> Stopped,
        // settled -> AtRest. Dedup must not eat any of these edges.
        CHECK(module_status_action_for_transition(ManagerState::Running, ManagerState::ShutdownRequested) ==
              ModuleStatusAction::Stopping);
        CHECK(module_status_action_for_transition(ManagerState::ForceTerminating, ManagerState::ShutdownFinalizing) ==
              ModuleStatusAction::Stopped);
        CHECK(module_status_action_for_transition(ManagerState::ShutdownFinalizing, ManagerState::Idle) ==
              ModuleStatusAction::AtRest);
        // Admin-panel restart drain: the escalation out of RestartRequested is a real phase change.
        CHECK(module_status_action_for_transition(ManagerState::RestartRequested, ManagerState::ForceTerminating) ==
              ModuleStatusAction::Stopping);
    }
}
