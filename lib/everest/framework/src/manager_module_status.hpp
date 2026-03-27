// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

/// @file manager_module_status.hpp
///
/// The manager lifecycle phase (`ManagerState`) and its mapping onto the module status reported to
/// the config service (`ModuleStatusAction`).
///
/// The mapping lives here, separate from `Manager`, so it can be unit tested without linking the
/// manager executable: it is a total function over an enum, which is exactly the kind of thing that
/// silently rots when it is buried in a lambda.
///
/// The full lifecycle description lives in `lib/everest/framework/docs/ManagerLifecycle.md`,
/// the state machine diagram in `lib/everest/framework/docs/ManagerLifecycleStateMachine.mmd`.
/// Timeouts and limits that drive transitions are defined in `manager.cpp`.

/// \brief Runtime phase of the manager main loop (see file-level state machine description).
enum class ManagerState {
    Initializing,
    StartingModules,
    Running,
    RestartRequested,
    CrashShutdownInProgress,
    ShutdownRequested,
    ForceTerminating,
    ShutdownFinalizing,
    Idle,
    Exiting
};

/// \brief What the config service should be told when the manager reaches a lifecycle phase.
///
/// `AtRest` is distinct from `Stopped`: it settles a transitional status but preserves a recorded
/// FailedToStart, which is itself a resting status. See ConfigServiceCore::set_modules_at_rest().
enum class ModuleStatusAction {
    Starting,
    Running,
    Stopping,
    Stopped,
    AtRest,
    RestartTriggered
};

/// \brief Total mapping ManagerState -> module status action.
///
/// Derived from the *destination* state alone. Deriving it from the transition (from, to) is what
/// previously allowed a transitional status to survive a transition that no branch matched: e.g.
/// ShutdownFinalizing -> Idle kept a FailedToStart forever (blocking the very reload that would
/// recover from it), and StartingModules -> ShutdownRequested kept reporting Starting for the whole
/// drain. A self-transition needs no action at all, since it cannot change the phase.
///
/// The switch is exhaustive on purpose and deliberately has NO default label, so that adding a
/// ManagerState enumerator has to be a decision rather than an omission. The unit test target builds
/// this header with -Werror=switch, so a missing case breaks the build there.
constexpr ModuleStatusAction module_status_action_for(ManagerState state) {
    switch (state) {
    case ManagerState::Initializing:
        // No module process has been spawned yet.
        return ModuleStatusAction::AtRest;
    case ManagerState::StartingModules:
        return ModuleStatusAction::Starting;
    case ManagerState::Running:
        return ModuleStatusAction::Running;
    case ManagerState::RestartRequested:
        // Admin-requested restart: the modules are draining and will be started again afterwards.
        return ModuleStatusAction::RestartTriggered;
    case ManagerState::ShutdownRequested:
    case ManagerState::CrashShutdownInProgress:
    case ManagerState::ForceTerminating:
        return ModuleStatusAction::Stopping;
    case ManagerState::ShutdownFinalizing:
        // All module PIDs are gone. Reporting "stopped" here rather than only at Idle is what lets
        // the config service reinitialize from the database during finalization, which is what the
        // restart path needs.
        return ModuleStatusAction::Stopped;
    case ManagerState::Idle:
    case ManagerState::Exiting:
        // Resting: settle any transitional status, but keep a FailedToStart recorded by the
        // failed-reload path - it is a resting status and the reason nothing is running.
        return ModuleStatusAction::AtRest;
    }
    return ModuleStatusAction::AtRest;
}
