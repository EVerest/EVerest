// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <boost/program_options/variables_map.hpp>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <unordered_map>
#include <vector>

namespace Everest {
class ManagerConfig;
class MQTTAbstraction;
class StatusFifo;
struct ManagerSettings;
namespace system {
class SignalPolling;
}
} // namespace Everest
struct TypedHandler;

class ManagerAdminPanel;

struct ModuleShutdownInfo {
    std::string id;
    int wstatus;
};

/// @file manager.hpp
///
/// `ManagerState` is the **phase** of the main-loop (what the manager is doing right now).
/// `ShutdownCause` records **why** a shutdown or drain was started; it is kept across transient
/// states (for example through `ForceTerminating` / `ShutdownFinalizing`) so the next step can
/// distinguish normal stop, admin-driven restart, and crash recovery.
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

/// \brief Why the current shutdown / drain was started (persists across some ManagerState values).
enum class ShutdownCause {
    None,
    Normal,
    Restart,
    Crash
};

class Manager {
public:
    /// \brief Construct manager with parsed CLI arguments.
    /// \param vm Parsed command line options used by manager startup/runtime.
    explicit Manager(const boost::program_options::variables_map& vm);

    /// \brief Start manager lifecycle and main event loop.
    /// \return Process exit code (EXIT_SUCCESS / EXIT_FAILURE).
    int run();

private:
    /// \brief Ready-subscription tracking for a single module.
    struct ModuleReadyInfo {
        bool ready{false};
        std::shared_ptr<TypedHandler> ready_token;
        std::shared_ptr<TypedHandler> get_config_token;
    };
    using ModulesReadyType = std::unordered_map<std::string, ModuleReadyInfo>;

    // Per-run dependencies passed through handlers to avoid long parameter lists
    // while keeping runtime data explicit (instead of hidden mutable members).
    /// \brief Aggregates runtime dependencies used across handlers for one run.
    struct RuntimeContext {
        std::shared_ptr<Everest::ManagerConfig>& config;
        Everest::MQTTAbstraction& mqtt_abstraction;
        const std::vector<std::string>& ignored_modules;
        const std::vector<std::string>& standalone_modules;
        const Everest::ManagerSettings& ms;
        Everest::StatusFifo& status_fifo;
        bool retain_topics;
    };

    /// \brief Outcome of one lifecycle state-advance evaluation.
    struct LifecycleAdvanceResult {
        enum class Status {
            NoTransition,
            TransitionApplied,
            ExitRequested
        };
        Status status{Status::NoTransition};
        std::optional<int> exit_code{};
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Setup/helpers
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    /// \brief Load and validate manager configuration from current boot source.
    /// \param ms Fully resolved manager settings for this run.
    /// \return Shared validated configuration object.
    std::shared_ptr<Everest::ManagerConfig> load_and_validate_config(const Everest::ManagerSettings& ms) const;

    /// \brief Create MQTT abstraction, connect, and spawn its main loop thread.
    /// \param ms Fully resolved manager settings for this run.
    /// \return Connected MQTT abstraction, or nullptr on connection failure.
    std::unique_ptr<Everest::MQTTAbstraction> create_and_connect_mqtt(const Everest::ManagerSettings& ms) const;

    /// \brief Collect standalone module ids from config plus CLI overrides.
    /// \param config Validated manager configuration for this run.
    /// \return Module ids that manager should not spawn automatically.
    std::vector<std::string> collect_standalone_modules(const Everest::ManagerConfig& config) const;

    /// \brief Collect ignored module ids from CLI overrides.
    /// \return Module ids that manager should ignore entirely during startup.
    std::vector<std::string> collect_ignored_modules() const;

    /// \brief Publish interfaces/types/settings/manifests metadata on MQTT.
    /// \param ctx Runtime dependencies for the current run.
    void publish_startup_metadata(const RuntimeContext& ctx) const;

    /// \brief Unregister all module ready handlers and clear ready-tracking state.
    void unregister_module_ready_handlers(Everest::ManagerConfig& config, Everest::MQTTAbstraction& mqtt_abstraction);

    /// \brief Unregister module ready handlers and clear retained MQTT topics.
    /// \note Must be called with the config that was used to register handlers (before any reload).
    /// \note MQTT must still be connected; call before any disconnect.
    void cleanup_modules_state(Everest::ManagerConfig& config, Everest::MQTTAbstraction& mqtt_abstraction);

    /// \brief Terminate remaining module processes (SIGTERM, then SIGKILL fallback).
    void shutdown_modules(const std::map<pid_t, std::string>& modules, Everest::ManagerConfig& config,
                          Everest::MQTTAbstraction& mqtt_abstraction);

    /// \brief Convert ManagerState enum to a readable string for logs.
    std::string_view state_to_string(ManagerState state) const;

    /// \brief Apply state transition with transition logging.
    void transition_to(ManagerState new_state);

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // State predicates
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    /// \brief True only when manager is fully running.
    bool are_modules_started() const;
    /// \brief True when manager is in any shutdown-related state.
    bool is_in_shutdown_flow_state() const;
    /// \brief True when restart has been requested.
    bool is_restart_requested() const;
    /// \brief True when crash-shutdown flow is active.
    bool is_crash_in_progress() const;
    /// \brief True when in idle.
    bool is_idle() const;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // State/event handlers
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    /// \brief Handle module startup: transition to StartingModules, register ready handlers, and spawn modules.
    /// \param ctx Runtime dependencies for the current run.
    /// \return Mapping of spawned child pid to module id.
    std::map<pid_t, std::string> handle_start_modules(const RuntimeContext& ctx);

    /// \brief Advance lifecycle state when current phase is complete.
    /// \param ctx Runtime dependencies for the current run.
    /// \param admin_panel Controller IPC/process integration helper.
    /// \return Result containing transition/exit outcome for this evaluation step.
    LifecycleAdvanceResult advance_lifecycle_state_if_ready(RuntimeContext& ctx, ManagerAdminPanel& admin_panel);

    /// \brief Complete shutdown finalization according to preserved restart/crash intent.
    /// \param ctx Runtime dependencies for the current run.
    /// \param admin_panel Controller IPC/process integration helper.
    /// \param restart_requested Preserved restart intent for this finalization step.
    /// \param crash_in_progress Preserved crash-recovery intent for this finalization step.
    /// \return Exit code when manager should terminate, std::nullopt otherwise.
    std::optional<int> handle_finalize_shutdown_transition(RuntimeContext& ctx, ManagerAdminPanel& admin_panel,
                                                           bool restart_requested, bool crash_in_progress);

    /// \brief Reload config and initiate module restart sequence.
    void handle_restart_modules_after_shutdown(RuntimeContext& ctx);

    /// \brief Format the entries of shutdown_info_ that did not exit cleanly for logging.
    /// \return Space-separated "id (wait status)" list, empty when all modules exited cleanly.
    std::string format_unclean_exits() const;

    /// \brief Reset all shutdown/drain bookkeeping state.
    void reset_shutdown_state();

    /// \brief Finalize normal shutdown and decide exit vs idle outcome.
    /// \param ctx Runtime dependencies for the current run.
    /// \param admin_panel Controller IPC/process integration helper.
    /// \return EXIT_SUCCESS/EXIT_FAILURE when manager exits, std::nullopt for idle mode.
    std::optional<int> handle_finish_normal_shutdown(RuntimeContext& ctx, ManagerAdminPanel& admin_panel);

    /// \brief Finalize crash-recovery shutdown path.
    /// \param ctx Runtime dependencies for the current run.
    /// \param admin_panel Controller IPC/process integration helper.
    /// \return EXIT_FAILURE when manager exits after crash (default), std::nullopt when staying idle
    ///         (`--recover-module-crashes` and restart cap exceeded).
    std::optional<int> handle_finish_crash_recovery(RuntimeContext& ctx, ManagerAdminPanel& admin_panel);

    /// \brief Start graceful shutdown and publish shutdown topic when required.
    /// \param module_exited_time Timestamp used as shutdown start reference.
    /// \param publish_when_sigint_received Whether to publish shutdown topic even after SIGINT.
    /// \param info_log Optional critical log message emitted before publish.
    /// \param mqtt_abstraction Active MQTT abstraction instance.
    /// \param ms Fully resolved manager settings for this run.
    void handle_initiate_graceful_shutdown(const std::chrono::steady_clock::time_point& module_exited_time,
                                           bool publish_when_sigint_received,
                                           const std::optional<std::string>& info_log,
                                           Everest::MQTTAbstraction& mqtt_abstraction,
                                           const Everest::ManagerSettings& ms);

    /// \brief Enforce shutdown timeout and force-terminate remaining modules.
    /// \param ctx Runtime dependencies for the current run.
    void handle_shutdown_timeout(RuntimeContext& ctx);

    /// \brief Poll waitpid once and dispatch child exit handling.
    /// \param wstatus waitpid status output parameter.
    /// \param ctx Runtime dependencies for the current run.
    /// \param admin_panel Controller IPC/process integration helper.
    /// \return true when loop should short-circuit/continue after handling.
    bool handle_waitpid_event(int& wstatus, RuntimeContext& ctx, ManagerAdminPanel& admin_panel);

    /// \brief Handle one child exit and update shutdown/restart state.
    /// \param pid Exited child process id.
    /// \param wstatus waitpid status for the exited child.
    /// \param ctx Runtime dependencies for the current run.
    /// \param admin_panel Controller IPC/process integration helper.
    /// \return true when loop should short-circuit/continue after handling.
    bool handle_child_exit(pid_t pid, int wstatus, RuntimeContext& ctx, ManagerAdminPanel& admin_panel);

    /// \brief Poll controller IPC commands (restart/check-config).
    /// \param ctx Runtime dependencies for the current run.
    /// \param admin_panel Controller IPC/process integration helper.
    /// \param prefix_opt Prefix passed to config-check requests.
    /// \return Exit code when manager should terminate, std::nullopt otherwise.
    std::optional<int> handle_controller_ipc_poll(RuntimeContext& ctx, ManagerAdminPanel& admin_panel,
                                                  const std::string& prefix_opt);

    /// \brief Handle SIGINT/SIGTERM transition and optional immediate exit.
    /// \param signo Signal number received by polling.
    /// \param ctx Runtime dependencies for the current run.
    /// \param admin_panel Controller IPC/process integration helper.
    /// \return Exit code when manager should terminate, std::nullopt otherwise.
    std::optional<int> handle_signal(int signo, RuntimeContext& ctx, ManagerAdminPanel& admin_panel);

    /// \brief Poll signal fd and forward handled signals to handle_signal().
    /// \param signal_polling Signal polling abstraction used by manager loop.
    /// \param ctx Runtime dependencies for the current run.
    /// \param admin_panel Controller IPC/process integration helper.
    /// \return Exit code when manager should terminate, std::nullopt otherwise.
    std::optional<int> handle_signal_poll(Everest::system::SignalPolling& signal_polling, RuntimeContext& ctx,
                                          ManagerAdminPanel& admin_panel);

    const boost::program_options::variables_map& vm_;
    bool recover_module_crashes_{false};
    ManagerState state_{ManagerState::Idle};
    ShutdownCause shutdown_cause_{ShutdownCause::None};
    bool sigint_received_{false};
    std::uint8_t unexpected_module_exit_count_{0};
    std::optional<std::chrono::steady_clock::time_point> shutdown_start_time_;
    std::optional<std::chrono::steady_clock::time_point> force_terminate_start_time_;
    bool force_kill_sent_{false};
    std::map<pid_t, std::string> module_handles_;
    std::vector<ModuleShutdownInfo> shutdown_info_;
    ModulesReadyType modules_ready_; // guarded by modules_ready_mutex_
    std::mutex modules_ready_mutex_;
    std::vector<std::function<void(ManagerState, ManagerState)>> state_transition_handlers_;

public:
    /// \brief Register a callback invoked on every state transition with (old_state, new_state).
    void register_state_transition_handler(std::function<void(ManagerState, ManagerState)> handler);
};
