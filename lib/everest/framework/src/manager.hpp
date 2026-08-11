// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <atomic>
#include <boost/program_options/variables_map.hpp>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <unordered_map>
#include <vector>

namespace everest::db::sqlite {
class ConnectionInterface;
} // namespace everest::db::sqlite

namespace Everest {
class ManagerConfig;
class MQTTAbstraction;
class StatusFifo;
struct ManagerSettings;
namespace system {
class SignalPolling;
}
namespace config {
class ConfigServiceCore;
}
} // namespace Everest
struct TypedHandler;

class ManagerAdminPanel;

struct ModuleShutdownInfo {
    std::string id;
    int wstatus;
};

// Data structure to keep MQTT Last-Will-and-Testament related items together
struct LwtCfg {
    std::string topic;
    std::string data;
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

/// \brief Deferred lifecycle-API intent recorded on the MQTT thread and consumed by the main loop.
///
/// The LifecycleAPI stop/restart command handlers run on an MQTT worker thread. Stop/Restart
/// are mutually exclusive, so a single atomic with last-writer-wins semantics is sufficient.
enum class LifecycleApiRequest {
    None,
    Stop,
    Restart
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
    };
    using ModulesReadyType = std::unordered_map<std::string, ModuleReadyInfo>;

    // Per-run dependencies passed through handlers to avoid long parameter lists
    // while keeping runtime data explicit (instead of hidden mutable members).
    /// \brief Aggregates runtime dependencies used across handlers for one run.
    struct RuntimeContext {
        std::shared_ptr<const Everest::ManagerConfig>& config;
        Everest::MQTTAbstraction& mqtt_abstraction;
        std::vector<std::string>& ignored_modules;
        std::vector<std::string>& standalone_modules;
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
    /// \param preloaded_module_configs Full module configuration, but maybe not validated yet
    /// \return Shared validated configuration object.
    std::shared_ptr<const Everest::ManagerConfig>
    load_and_validate_config(const Everest::ManagerSettings& ms,
                             everest::config::ModuleConfigurations& preloaded_module_configs) const;

    /// \brief Create MQTT abstraction, connect, and spawn its main loop thread.
    /// \param ms Fully resolved manager settings for this run.
    /// \param lwt_cfg Optional Last-Will-and-Testament
    /// \return Connected MQTT abstraction, or nullptr on connection failure.
    std::unique_ptr<Everest::MQTTAbstraction> create_and_connect_mqtt(const Everest::ManagerSettings& ms) const;
    std::unique_ptr<Everest::MQTTAbstraction> create_and_connect_mqtt(const Everest::ManagerSettings& ms,
                                                                      std::optional<LwtCfg> lwt_cfg) const;

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
    void unregister_module_ready_handlers(const Everest::ManagerConfig& config,
                                          Everest::MQTTAbstraction& mqtt_abstraction);

    /// \brief Unregister module ready handlers and clear retained MQTT topics.
    /// \note Must be called with the config that was used to register handlers (before any reload).
    /// \note MQTT must still be connected; call before any disconnect.
    void cleanup_modules_state(const Everest::ManagerConfig& config, Everest::MQTTAbstraction& mqtt_abstraction);

    /// \brief Terminate remaining module processes (SIGTERM, then SIGKILL fallback).
    void shutdown_modules(const std::map<pid_t, std::string>& modules, const Everest::ManagerConfig& config,
                          Everest::MQTTAbstraction& mqtt_abstraction);

    /// \brief Convert ManagerState enum to a readable string for logs.
    std::string_view state_to_string(ManagerState state) const;

    /// \brief Apply state transition with transition logging.
    void transition_to(ManagerState new_state);

    /// \brief Like transition_to(); caller must hold m_state_transition_mutex.
    void transition_to_unlocked(ManagerState new_state);

    /// \brief Write a status-fifo message when a fifo path was configured for this run.
    void notify_status_fifo(std::string_view message);

    /// \brief Write the status-fifo message that corresponds to \p state.
    void notify_status_fifo_for_state(ManagerState state);

    /// \brief Write CRASH_RECOVERY_ATTEMPT:n/max to the status fifo.
    void notify_crash_recovery_attempt(std::uint8_t attempt, std::uint8_t max);

    /// \brief Like is_in_shutdown_flow_state(); caller must hold m_state_transition_mutex.
    bool is_in_shutdown_flow_state_unlocked() const;

    /// \brief Load current state; caller must hold m_state_transition_mutex.
    ManagerState current_state_unlocked() const;
    /// \brief Reload the configuration from the config_service_core class and update relevant fields in the context
    /// \return Updated context to a valid configuration
    bool reload_and_update_context(RuntimeContext& ctx);

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // State predicates
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    /// \brief True only when manager is fully running.
    bool are_modules_started() const;
    /// \brief True when manager is in any shutdown-related state.
    bool is_in_shutdown_flow_state() const;
    /// \brief True when restart has been requested.
    bool is_restart_requested() const;
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

    /// \brief Format the entries of m_shutdown_info that did not exit cleanly for logging.
    /// \return Space-separated "id (wait status)" list, empty when all modules exited cleanly.
    std::string format_unclean_exits() const;

    /// \brief Reset all shutdown/drain bookkeeping state.
    void reset_shutdown_state();

    /// \brief Transition to Idle after modules have shut down; logs \p log_message.
    /// \return std::nullopt for callers that return std::optional<int>.
    std::optional<int> transition_to_idle_after_shutdown(std::string_view log_message);

    /// \brief Disconnect controller and MQTT, optionally reset shutdown state, transition to Exiting.
    /// \return Process exit code for the caller.
    int transition_to_exiting_after_shutdown(RuntimeContext& ctx, ManagerAdminPanel& admin_panel, int exit_code,
                                             bool reset_state);

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

    /// \brief Start the shutdown flow; publishes the shutdown topic when graceful shutdown is
    ///        enabled (`--graceful-shutdown`), otherwise modules are force-terminated by the
    ///        zero-deadline path in handle_shutdown_timeout().
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

    /// \brief Main-loop signal poll timeout: short while deadlines are running or controller IPC
    ///        needs polling, long otherwise (SIGINT/SIGTERM/SIGCHLD wake the poll immediately).
    int signal_poll_timeout_ms() const;

    /// \brief Register a callback invoked on every state transition with (old_state, new_state).
    void register_state_transition_handler(std::function<void(ManagerState, ManagerState)> handler);

    /// \brief Wake the main-loop poll after recording a lifecycle-API request (MQTT-thread safe).
    void poke_lifecycle_wakeup();

    /// \brief Consume a deferred lifecycle-API stop/restart request on the main loop.
    /// \param ctx Runtime dependencies for the current run.
    void handle_lifecycle_api_request(RuntimeContext& ctx);

    const boost::program_options::variables_map& m_vm;
    Everest::StatusFifo* m_status_fifo{nullptr};
    bool m_recover_module_crashes{false};
    // Opt-in via --graceful-shutdown: publish the MQTT shutdown signal and give modules
    // SHUTDOWN_TIMEOUT_MS to exit on their own. Default (false): terminate module processes
    // immediately (SIGTERM, escalating to SIGKILL after FORCE_KILL_GRACE_TIMEOUT_MS).
    bool m_graceful_shutdown_enabled{false};
    // m_state is atomic because the module-ready handler runs on the MQTT thread; transitions are
    // serialized with m_state_transition_mutex (main loop and ready handler).
    std::atomic<ManagerState> m_state{ManagerState::Idle};
    ShutdownCause m_shutdown_cause{ShutdownCause::None};
    std::atomic<bool> m_sigint_received{false};
    // Deferred lifecycle-API intent: set on the MQTT worker thread by the stop/restart command
    // handlers, consumed on the main loop by handle_lifecycle_api_request(). Keeps all mutation of
    // m_module_handles/shutdown_* on the main thread. Last-writer-wins (Stop/Restart exclusive).
    std::atomic<LifecycleApiRequest> m_lifecycle_api_request{LifecycleApiRequest::None};
    // eventfd owned by run(): the MQTT stop/restart handlers write to it after setting
    // m_lifecycle_api_request to wake up the main-loop poll() immediately.
    // -1 when not yet created / after run() returns.
    int m_lifecycle_wakeup_fd{-1};
    // Unexpected-exit recovery attempts for this manager process lifetime (current config).
    // Not cleared on transition to Running; resets when run() starts (future: also on config change).
    std::uint8_t m_unexpected_module_exit_count{0};
    std::chrono::steady_clock::time_point m_module_startup_start_time{std::chrono::steady_clock::now()};
    std::optional<std::chrono::steady_clock::time_point> m_shutdown_start_time;
    std::optional<std::chrono::steady_clock::time_point> m_force_terminate_start_time;
    bool m_force_kill_sent{false};
    std::map<pid_t, std::string> m_module_handles;
    std::vector<ModuleShutdownInfo> m_shutdown_info;
    ModulesReadyType m_modules_ready; // guarded by m_modules_ready_mutex
    std::mutex m_modules_ready_mutex;
    mutable std::mutex m_state_transition_mutex;
    std::vector<std::function<void(ManagerState, ManagerState)>> m_state_transition_handlers;
    std::shared_ptr<everest::db::sqlite::ConnectionInterface> m_db_connection;
    std::unique_ptr<Everest::config::ConfigServiceCore> m_config_service_core{};
};
