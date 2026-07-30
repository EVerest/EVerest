// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>

#include <cstdlib>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <boost/exception/diagnostic_information.hpp>
#include <boost/program_options.hpp>

#include <fmt/color.h>
#include <fmt/core.h>
#include <fmt/ranges.h>

#include <everest/logging.hpp>
#include <framework/everest.hpp>
#include <framework/runtime.hpp>
#include <utils/config.hpp>
#include <utils/config/config_service_core.hpp>
#include <utils/config/slot_manager.hpp>
#include <utils/date.hpp>
#include <utils/mqtt_abstraction.hpp>
#include <utils/status_fifo.hpp>

#include "manager.hpp"
#include "manager_admin_panel.hpp"
#include "system_unix.hpp"
#include <generated/version_information.hpp>

namespace po = boost::program_options;
namespace fs = std::filesystem;

using namespace Everest;

// While no shutdown/force-kill deadline is running the main loop only has to react to signals
// (SIGINT/SIGTERM/SIGCHLD wake the signal fd poll immediately), so it can block for a long time
// instead of ticking every SIGNAL_POLL_TIMEOUT_MS.
const auto IDLE_SIGNAL_POLL_TIMEOUT_MS = 60000;
const auto SIGNAL_POLL_TIMEOUT_MS = 50;
// Graceful-shutdown deadline before force-terminating modules; only applies with
// --graceful-shutdown. Without the flag the deadline is zero and modules are terminated
// immediately when the shutdown flow starts.
const auto SHUTDOWN_TIMEOUT_MS = 5000;
const auto FORCE_KILL_GRACE_TIMEOUT_MS = 5000;
const std::uint8_t MAX_UNEXPECTED_MODULE_RESTARTS = 3;

// Helper struct keeping information on how to start module
struct ModuleStartInfo {
    enum class Language {
        cpp,
        javascript,
        python
    };
    ModuleStartInfo(const std::string& name_, const std::string& printable_name_, Language lang_, const fs::path& path_,
                    std::vector<std::string> capabilities_) :
        name(name_),
        printable_name(printable_name_),
        language(lang_),
        path(path_),
        capabilities(std::move(capabilities_)) {
    }
    std::string name;
    std::string printable_name;
    Language language;
    fs::path path;

    // required capabilities of this module
    std::vector<std::string> capabilities;
};

namespace {

// Anonymous-namespace helpers used by Manager::run() and module spawn paths.

/// \brief Convert a wait status code to a compact readable string.
std::string format_wait_status(int status) {
    if (WIFEXITED(status)) {
        return fmt::format("exit_code={}", WEXITSTATUS(status));
    }
    if (WIFSIGNALED(status)) {
        return fmt::format("signal={}", WTERMSIG(status));
    }
    return fmt::format("raw={}", status);
}

/// \brief Return true if process exited normally with code 0.
bool is_clean_exit(int status) {
    return WIFEXITED(status) && (WEXITSTATUS(status) == 0);
}

/// \brief Setup common environment variables for everestjs and everestpy
void setup_environment(const ModuleStartInfo& module_info, const RuntimeSettings& rs,
                       const MQTTSettings& mqtt_settings) {
    setenv(EV_MODULE, module_info.name.c_str(), 1);
    setenv(EV_PREFIX, rs.prefix.c_str(), 0);
    setenv(EV_LOG_CONF_FILE, rs.logging_config_file.c_str(), 0);
    setenv(EV_MQTT_EVEREST_PREFIX, mqtt_settings.everest_prefix.c_str(), 0);
    setenv(EV_MQTT_EXTERNAL_PREFIX, mqtt_settings.external_prefix.c_str(), 0);
    if (mqtt_settings.uses_socket()) {
        setenv(EV_MQTT_BROKER_SOCKET_PATH, mqtt_settings.broker_socket_path.c_str(), 0);
    } else {
        setenv(EV_MQTT_BROKER_HOST, mqtt_settings.broker_host.c_str(), 0);
        setenv(EV_MQTT_BROKER_PORT, std::to_string(mqtt_settings.broker_port).c_str(), 0);
    }

    if (rs.validate_schema) {
        setenv(EV_VALIDATE_SCHEMA, "1", 1);
    }
}

/// \brief Execute a module process and report exec failures to parent.
static void exec_module_binary(const std::string& bin, std::vector<std::string>& arguments,
                               system::SubProcess& proc_handle) {
    // Convert the argument list to the format required by `execv*()`.
    std::vector<char*> argv_list(arguments.size() + 1);
    std::transform(arguments.begin(), arguments.end(), argv_list.begin(), [](auto& value) { return value.data(); });
    argv_list.back() = nullptr; // Add a null terminator

    // Execute the module binary, replacing the current process.
    execvp(bin.c_str(), argv_list.data());

    // `execv()` failed, notify the parent process and exit.
    const auto msg = fmt::format("Syscall to execv() with \"{} {}\" failed ({})", bin,
                                 fmt::join(arguments.begin() + 1, arguments.end(), " "), strerror(errno));
    proc_handle.send_error_and_exit(msg);
}

/// \brief Build argv and execute a C++ module binary.
void exec_cpp_module(system::SubProcess& proc_handle, const ModuleStartInfo& module_info, const RuntimeSettings& rs,
                     const MQTTSettings& mqtt_settings) {
    std::vector<std::string> arguments = {
        module_info.printable_name,
        "--prefix",
        rs.prefix.string(),
        "--module",
        module_info.name,
        "--log_config",
        rs.logging_config_file.string(),
        "--mqtt_everest_prefix",
        mqtt_settings.everest_prefix,
        "--mqtt_external_prefix",
        mqtt_settings.external_prefix}; // TODO: check if this is empty and do not append if needed?

    if (mqtt_settings.uses_socket()) {
        arguments.insert(arguments.end(), {"--mqtt_broker_socket_path", mqtt_settings.broker_socket_path});
    } else {
        arguments.insert(arguments.end(), {"--mqtt_broker_host", mqtt_settings.broker_host, "--mqtt_broker_port",
                                           std::to_string(mqtt_settings.broker_port)});
    }

    exec_module_binary(module_info.path.string(), arguments, proc_handle);
}

/// \brief Prepare environment and execute a JavaScript module via node.
void exec_javascript_module(system::SubProcess& proc_handle, const ModuleStartInfo& module_info,
                            const RuntimeSettings& rs, const MQTTSettings& mqtt_settings) {
    // FIXME (aw): everest directory layout
    const auto node_modules_path = rs.prefix / defaults::LIB_DIR / defaults::NAMESPACE / "node_modules";
    setenv("NODE_PATH", node_modules_path.c_str(), 0);
    setup_environment(module_info, rs, mqtt_settings);

    std::vector<std::string> arguments = {
        "node",
        "--unhandled-rejections=strict",
        module_info.path.string(),
    };

    exec_module_binary("node", arguments, proc_handle);
}

/// \brief Prepare environment and execute a Python module.
void exec_python_module(system::SubProcess& proc_handle, const ModuleStartInfo& module_info, const RuntimeSettings& rs,
                        const MQTTSettings& mqtt_settings) {
    setup_environment(module_info, rs, mqtt_settings);

    // Prepend the everestpy path to $PYTHONPATH. This ensures modules can always find everestpy.
    const auto everestpy_path = rs.prefix / defaults::LIB_DIR / defaults::NAMESPACE / "everestpy";
    if (const auto prev_pythonpath = std::getenv("PYTHONPATH")) {
        const auto pythonpath = fmt::format("{}:{}", everestpy_path.string(), prev_pythonpath);
        setenv("PYTHONPATH", pythonpath.c_str(), 1);
    } else {
        setenv("PYTHONPATH", everestpy_path.c_str(), 1);
    }

    std::string python_binary = "python3";

    // Check if a virtual environment exists in the module directory, and if so use its python runtime.
    const auto venv_dir = module_info.path.parent_path() / ".venv";
    if (fs::exists(venv_dir)) {
        const auto venv_bin_dir = venv_dir / "bin";
        const auto venv_python = venv_bin_dir / "python3";
        if (fs::exists(venv_python)) {
            // Activate the virtual environment. This approximates the behaviour of the `.venv/bin/activate` script.
            python_binary = venv_python.string();
            setenv("VIRTUAL_ENV", venv_dir.c_str(), 1);
            setenv("VIRTUAL_ENV_PROMPT", "venv", 1);
            unsetenv("PYTHONHOME");

            if (const auto prev_path = std::getenv("PATH")) {
                const auto path = fmt::format("{}:{}", venv_bin_dir.string(), prev_path);
                setenv("PATH", path.c_str(), 1);
            } else {
                setenv("PATH", venv_bin_dir.c_str(), 1);
            }
        }
    }

    std::vector<std::string> arguments = {python_binary, module_info.path.c_str()};
    exec_module_binary(python_binary, arguments, proc_handle);
}

/// \brief Dispatch module execution to the language-specific executor.
void exec_module(const RuntimeSettings& rs, const MQTTSettings& mqtt_settings, const ModuleStartInfo& module,
                 system::SubProcess& proc_handle) {
    switch (module.language) {
    case ModuleStartInfo::Language::cpp:
        exec_cpp_module(proc_handle, module, rs, mqtt_settings);
        break;
    case ModuleStartInfo::Language::javascript:
        exec_javascript_module(proc_handle, module, rs, mqtt_settings);
        break;
    case ModuleStartInfo::Language::python:
        exec_python_module(proc_handle, module, rs, mqtt_settings);
        break;
    default:
        throw std::logic_error("Module language not in enum");
        break;
    }
}

/// \brief Spawn configured module processes and return pid-to-module mapping.
std::map<pid_t, std::string> spawn_modules(const std::vector<ModuleStartInfo>& modules, const ManagerSettings& ms) {
    std::map<pid_t, std::string> started_modules;

    const auto& rs = ms.runtime_settings;

    for (const auto& module : modules) {

        auto proc_handle = system::SubProcess::create(ms.run_as_user, module.capabilities);

        if (proc_handle.is_child()) {
            // first, check if we need any capabilities

            try {
                exec_module(rs, ms.mqtt_settings, module, proc_handle);
            } catch (const std::exception& err) {
                proc_handle.send_error_and_exit(err.what());
            }
        }

        // we can only come here, if we're the parent!
        const auto child_pid = proc_handle.check_child_executed();

        EVLOG_debug << fmt::format("Forked module {} with pid: {}", module.name, child_pid);
        started_modules[child_pid] = module.name;
    }

    return started_modules;
}

} // namespace

/// \brief Publish startup metadata, register handlers, and spawn module processes.
void Manager::publish_startup_metadata(const RuntimeContext& ctx) const {
    const auto& config = *ctx.config;
    auto& mqtt_abstraction = ctx.mqtt_abstraction;
    const auto& ms = ctx.ms;

    const auto interface_definitions = config.get_interface_definitions();
    std::vector<std::string> interface_names;
    for (auto& interface_definition : interface_definitions.items()) {
        interface_names.push_back(interface_definition.key());
    }

    MqttMessagePayload payload{MqttMessageType::ConfigurationResponse, interface_names};

    mqtt_abstraction.publish(fmt::format("{}interfaces", ms.mqtt_settings.everest_prefix), payload, QOS::QOS2, true);

    for (const auto& interface_definition : interface_definitions.items()) {

        MqttMessagePayload interface_definition_payload{MqttMessageType::ConfigurationResponse,
                                                        interface_definition.value()};
        mqtt_abstraction.publish(
            fmt::format("{}interface_definitions/{}", ms.mqtt_settings.everest_prefix, interface_definition.key()),
            interface_definition_payload, QOS::QOS2, true);
    }

    const auto type_definitions = config.get_types();
    std::vector<std::string> type_names;
    for (auto& type_definition : type_definitions.items()) {
        type_names.push_back(type_definition.key());
    }

    MqttMessagePayload type_names_payload{MqttMessageType::ConfigurationResponse, type_names};

    mqtt_abstraction.publish(fmt::format("{}types", ms.mqtt_settings.everest_prefix), type_names_payload, QOS::QOS2,
                             true);
    for (const auto& type_definition : type_definitions.items()) {

        MqttMessagePayload type_definition_payload{MqttMessageType::ConfigurationResponse, type_definition.value()};

        // type_definition keys already start with a / so omit it in the topic name
        mqtt_abstraction.publish(
            fmt::format("{}type_definitions{}", ms.mqtt_settings.everest_prefix, type_definition.key()),
            type_definition_payload, QOS::QOS2, true);
    }

    const auto settings = config.get_settings();

    MqttMessagePayload settings_payload{MqttMessageType::ConfigurationResponse, settings};

    mqtt_abstraction.publish(fmt::format("{}settings", ms.mqtt_settings.everest_prefix), settings_payload, QOS::QOS2,
                             true);

    if (ms.runtime_settings.validate_schema) {
        const auto schemas = config.get_schemas();

        MqttMessagePayload schemas_payload{MqttMessageType::ConfigurationResponse, schemas};

        mqtt_abstraction.publish(fmt::format("{}schemas", ms.mqtt_settings.everest_prefix), schemas_payload, QOS::QOS2,
                                 true);
    }

    const auto manifests = config.get_manifests();
    for (const auto& manifest : manifests.items()) {
        auto manifest_copy = manifest.value();
        manifest_copy.erase("config");

        MqttMessagePayload manifest_payload{MqttMessageType::ConfigurationResponse, manifest_copy};

        mqtt_abstraction.publish(fmt::format("{}manifests/{}", ms.mqtt_settings.everest_prefix, manifest.key()),
                                 manifest_payload, QOS::QOS2, true);
    }

    const auto module_names = config.get_module_names();

    MqttMessagePayload module_names_payload{MqttMessageType::ConfigurationResponse, module_names};

    mqtt_abstraction.publish(fmt::format("{}module_names", ms.mqtt_settings.everest_prefix), module_names_payload,
                             QOS::QOS2, true);
}

/// \brief Unregister all module ready handlers and clear tracked ready state.
void Manager::unregister_module_ready_handlers(const ManagerConfig& config, MQTTAbstraction& mqtt_abstraction) {
    ModulesReadyType modules_ready_moved;
    {
        const std::lock_guard<std::mutex> lck(m_modules_ready_mutex);
        modules_ready_moved = std::move(m_modules_ready);
        // Probably not needed after our move but lets be explicit.
        m_modules_ready.clear();
    }

    for (const auto& module : modules_ready_moved) {
        const auto& ready_info = module.second;
        if (!ready_info.ready_token) {
            // Skip entries from a partial startup that never got a token assigned.
            continue;
        }
        const auto& module_name = module.first;
        const std::string topic = fmt::format("{}/ready", config.mqtt_module_prefix(module_name));
        mqtt_abstraction.unregister_handler(topic, ready_info.ready_token);
    }
}

void Manager::cleanup_modules_state(const ManagerConfig& config, MQTTAbstraction& mqtt_abstraction) {
    unregister_module_ready_handlers(config, mqtt_abstraction);
    mqtt_abstraction.clear_retained_topics();
}

/// \brief Stop all remaining module processes, escalating SIGTERM to SIGKILL.
void Manager::shutdown_modules(const std::map<pid_t, std::string>& modules, const ManagerConfig& config,
                               MQTTAbstraction& mqtt_abstraction) {

    unregister_module_ready_handlers(config, mqtt_abstraction);

    for (const auto& child : modules) {
        auto retval = kill(child.first, SIGTERM);
        // FIXME (aw): supply errno strings
        if (retval != 0) {
            EVLOG_critical << fmt::format("SIGTERM of child: {} (pid: {}) {}: {}. Escalating to SIGKILL", child.second,
                                          child.first, fmt::format(TERMINAL_STYLE_ERROR, "failed"), retval);
            retval = kill(child.first, SIGKILL);
            if (retval != 0) {
                EVLOG_critical << fmt::format("SIGKILL of child: {} (pid: {}) {}: {}.", child.second, child.first,
                                              fmt::format(TERMINAL_STYLE_ERROR, "failed"), retval);
            } else {
                EVLOG_info << fmt::format("SIGKILL of child: {} (pid: {}) {}.", child.second, child.first,
                                          fmt::format(TERMINAL_STYLE_OK, "succeeded"));
            }
        } else {
            EVLOG_info << fmt::format("SIGTERM of child: {} (pid: {}) {}.", child.second, child.first,
                                      fmt::format(TERMINAL_STYLE_OK, "succeeded"));
        }
    }
}

namespace {

/// \brief Disconnect MQTT before the manager process exits (after controller shutdown).
void disconnect_mqtt(MQTTAbstraction& mqtt_abstraction) {
    mqtt_abstraction.disconnect();
}

/// \brief Print startup banner and version information.
void print_start_message(const std::string& version_information) {
    EVLOG_info << "  \033[0;1;35;95m_\033[0;1;31;91m__\033[0;1;33;93m__\033[0;1;32;92m__\033[0;1;36;96m_\033[0m      "
                  "\033[0;1;31;91m_\033[0;1;33;93m_\033[0m                \033[0;1;36;96m_\033[0m   ";
    EVLOG_info << " \033[0;1;31;91m|\033[0m  \033[0;1;33;93m_\033[0;1;32;92m__\033[0;1;36;96m_\\\033[0m "
                  "\033[0;1;34;94m\\\033[0m    \033[0;1;33;93m/\033[0m \033[0;1;32;92m/\033[0m               "
                  "\033[0;1;34;94m|\033[0m \033[0;1;35;95m|\033[0m";
    EVLOG_info
        << " \033[0;1;33;93m|\033[0m \033[0;1;32;92m|_\033[0;1;36;96m_\033[0m   \033[0;1;35;95m\\\033[0m "
           "\033[0;1;31;91m\\\033[0m  \033[0;1;33;93m/\033[0m \033[0;1;32;92m/\033[0;1;36;96m__\033[0m "
           "\033[0;1;34;94m_\033[0m \033[0;1;35;95m_\033[0;1;31;91m_\033[0m \033[0;1;33;93m__\033[0;1;32;92m_\033[0m  "
           "\033[0;1;36;96m_\033[0;1;34;94m__\033[0;1;35;95m|\033[0m \033[0;1;31;91m|_\033[0m";
    EVLOG_info << " \033[0;1;32;92m|\033[0m  \033[0;1;36;96m_\033[0;1;34;94m_|\033[0m   \033[0;1;31;91m\\\033[0m "
                  "\033[0;1;33;93m\\\033[0;1;32;92m/\033[0m \033[0;1;36;96m/\033[0m \033[0;1;34;94m_\033[0m "
                  "\033[0;1;35;95m\\\033[0m \033[0;1;31;91m'_\033[0;1;33;93m_/\033[0m \033[0;1;32;92m_\033[0m "
                  "\033[0;1;36;96m\\\033[0;1;34;94m/\033[0m \033[0;1;35;95m__\033[0;1;31;91m|\033[0m "
                  "\033[0;1;33;93m__\033[0;1;32;92m|\033[0m";
    EVLOG_info << " \033[0;1;36;96m|\033[0m \033[0;1;34;94m|_\033[0;1;35;95m__\033[0;1;31;91m_\033[0m   "
                  "\033[0;1;32;92m\\\033[0m  \033[0;1;36;96m/\033[0m  \033[0;1;35;95m__\033[0;1;31;91m/\033[0m "
                  "\033[0;1;33;93m|\033[0m \033[0;1;32;92m|\033[0m  "
                  "\033[0;1;36;96m_\033[0;1;34;94m_/\033[0;1;35;95m\\_\033[0;1;31;91m_\033[0m \033[0;1;33;93m\\\033[0m "
                  "\033[0;1;32;92m|_\033[0m";
    EVLOG_info << " \033[0;1;34;94m|_\033[0;1;35;95m__\033[0;1;31;91m__\033[0;1;33;93m_|\033[0m   "
                  "\033[0;1;36;96m\\\033[0;1;34;94m/\033[0m "
                  "\033[0;1;35;95m\\_\033[0;1;31;91m__\033[0;1;33;93m|_\033[0;1;32;92m|\033[0m  "
                  "\033[0;1;36;96m\\\033[0;1;34;94m__\033[0;1;35;95m_|\033[0;1;31;91m|_\033[0;1;33;93m__\033[0;1;32;"
                  "92m/\\\033[0;1;36;96m__\033[0;1;34;94m|\033[0m";
    EVLOG_info << "";
    EVLOG_info << PROJECT_NAME << " " << PROJECT_VERSION << " " << GIT_VERSION;
    EVLOG_info << version_information;
    EVLOG_info << "";
}

/// \brief Print final shutdown message including elapsed shutdown duration.
void print_shutdown_message(const std::optional<std::chrono::steady_clock::time_point> shutdown_start_time,
                            const std::string& message_prefix = "") {
    auto shutdown_duration = 0LL;
    if (shutdown_start_time.has_value()) {
        shutdown_duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                                  shutdown_start_time.value())
                                .count();
    } else {
        EVLOG_info << "shutdown start time is not set?";
    }
    EVLOG_info << fmt::format(
        TERMINAL_STYLE_ERROR, "👋👋👋 {}{}", message_prefix,
        fmt::format(TERMINAL_STYLE_ERROR, "EVerest manager is exiting [{}ms] 👋👋👋", shutdown_duration));
}

} // namespace

// ---- State/event handlers ---------------------------------------------------

std::string Manager::format_unclean_exits() const {
    std::string bad_modules;
    for (const auto& info : m_shutdown_info) {
        if (!is_clean_exit(info.wstatus)) {
            bad_modules += fmt::format(" {} ({})", info.id, format_wait_status(info.wstatus));
        }
    }
    return bad_modules;
}

void Manager::reset_shutdown_state() {
    m_shutdown_info.clear();
    m_shutdown_start_time = std::nullopt;
    m_shutdown_cause = ShutdownCause::None;
    m_force_terminate_start_time = std::nullopt;
    m_force_kill_sent = false;
}

std::optional<int> Manager::transition_to_idle_after_shutdown(std::string_view log_message) {
    transition_to(ManagerState::Idle);
    EVLOG_info << log_message;
    return std::nullopt;
}

int Manager::transition_to_exiting_after_shutdown(RuntimeContext& ctx, ManagerAdminPanel& admin_panel, int exit_code,
                                                  bool reset_state) {
    admin_panel.shutdown_controller();
    disconnect_mqtt(ctx.mqtt_abstraction);
    if (reset_state) {
        reset_shutdown_state();
    }
    transition_to(ManagerState::Exiting);
    return exit_code;
}

void Manager::handle_restart_modules_after_shutdown(RuntimeContext& ctx) {
    // Cleanup with the OLD config before the reload below. Required because this function is also
    // called from advance_lifecycle_state_if_ready() (crash-with-restart path) which does not go
    // through handle_finish_* finalize functions.
    cleanup_modules_state(*ctx.config, ctx.mqtt_abstraction);
    if (reload_and_update_context(ctx)) {
        m_module_handles = handle_start_modules(ctx);
        m_shutdown_cause = ShutdownCause::None;
        m_shutdown_start_time = std::nullopt;
        m_force_terminate_start_time = std::nullopt;
        m_force_kill_sent = false;
        m_shutdown_info.clear();
        EVLOG_info << "Modules restart initiated with reloaded configuration.";
    } else {
        EVLOG_error << "Failed to reload the configuration.";
        m_config_service_core->notice_cfg_validation_failed();
        transition_to(ManagerState::Idle);
    }
}

std::optional<int> Manager::handle_finish_normal_shutdown(RuntimeContext& ctx, ManagerAdminPanel& admin_panel) {
    const std::string bad_modules = format_unclean_exits();
    // Cleanup module state while MQTT is still connected (must be before disconnect_mqtt() in Exiting path).
    cleanup_modules_state(*ctx.config, ctx.mqtt_abstraction);
    if (m_sigint_received) {
        if (bad_modules.empty()) {
            notify_status_fifo(StatusFifo::ALL_MODULES_STOPPED_CLEAN);
            print_shutdown_message(m_shutdown_start_time,
                                   fmt::format(TERMINAL_STYLE_OK, "All modules shut down properly. "));
        } else {
            EVLOG_warning << "Modules that did not shut down cleanly:" << bad_modules;
            print_shutdown_message(m_shutdown_start_time);
        }
        return transition_to_exiting_after_shutdown(ctx, admin_panel, EXIT_SUCCESS, true);
    }

    if (!bad_modules.empty()) {
        EVLOG_warning << "Modules that did not shut down cleanly:" << bad_modules;
    }
    reset_shutdown_state();
    return transition_to_idle_after_shutdown("Manager is idle after module shutdown. Send SIGINT/SIGTERM to stop.");
}

std::optional<int> Manager::handle_finish_crash_recovery(RuntimeContext& ctx, ManagerAdminPanel& admin_panel) {
    const auto duration_ms = m_shutdown_start_time.has_value()
                                 ? std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - m_shutdown_start_time.value())
                                       .count()
                                 : 0;
    const std::string bad_modules = format_unclean_exits();
    if (bad_modules.empty()) {
        EVLOG_info << fmt::format("All {} modules shut down gracefully after crash [{}ms].", m_shutdown_info.size(),
                                  duration_ms);
    } else {
        EVLOG_warning << fmt::format(
            "Crash recovery shutdown completed in {} ms. Modules that did not shut down cleanly: {}", duration_ms,
            bad_modules);
    }

    cleanup_modules_state(*ctx.config, ctx.mqtt_abstraction);
    m_shutdown_info.clear();
    m_shutdown_start_time = std::nullopt;
    m_shutdown_cause = ShutdownCause::None;
    m_force_terminate_start_time = std::nullopt;
    m_force_kill_sent = false;

    // Stay idle only when the user has not requested a stop via SIGINT/SIGTERM.
    if (m_recover_module_crashes && !m_sigint_received) {
        return transition_to_idle_after_shutdown(
            "Crash recovery completed, manager is idle after module shutdown. Send SIGINT/SIGTERM to stop.");
    }

    EVLOG_critical << "Unexpected module exit; manager is exiting.";
    return transition_to_exiting_after_shutdown(ctx, admin_panel, EXIT_FAILURE, false);
}

std::optional<int> Manager::handle_finalize_shutdown_transition(RuntimeContext& ctx, ManagerAdminPanel& admin_panel,
                                                                bool restart_requested, bool crash_in_progress) {
    if (crash_in_progress) {
        return handle_finish_crash_recovery(ctx, admin_panel);
    }
    if (restart_requested) {
        handle_restart_modules_after_shutdown(ctx);
        return std::nullopt;
    }
    return handle_finish_normal_shutdown(ctx, admin_panel);
}

void Manager::handle_initiate_graceful_shutdown(const std::chrono::steady_clock::time_point& module_exited_time,
                                                bool publish_when_sigint_received,
                                                const std::optional<std::string>& info_log,
                                                MQTTAbstraction& mqtt_abstraction, const ManagerSettings& ms) {
    if (is_in_shutdown_flow_state() or is_idle()) {
        return;
    }
    transition_to(ManagerState::ShutdownRequested);
    m_shutdown_start_time = module_exited_time;
    if (publish_when_sigint_received or not m_sigint_received) {
        if (info_log.has_value()) {
            EVLOG_critical << info_log.value();
        }
        if (m_graceful_shutdown_enabled) {
            mqtt_abstraction.publish(fmt::format("{}shutdown", ms.mqtt_settings.everest_prefix), std::string("true"),
                                     QOS::QOS2, false);
        }
    }
}

bool Manager::reload_and_update_context(RuntimeContext& ctx) {
    m_config_service_core->reinitialize_from_db();
    auto module_cfg_ptr = m_config_service_core->get_active_module_configurations();
    // create a copy, because load_and_validate_config below will take ownership
    everest::config::ModuleConfigurations module_cfg = *module_cfg_ptr;

    std::shared_ptr<const ManagerConfig> config;
    try {
        config = load_and_validate_config(ctx.ms, module_cfg);
    } catch (const std::exception& e) {
        EVLOG_error << "Failed to load and validate the module configuration: " << e.what();
        return false;
    } catch (...) {
        EVLOG_error << "Failed to load and validate the module configuration (unknown error).";
        return false;
    }

    ctx.standalone_modules = collect_standalone_modules(*config);
    ctx.ignored_modules = collect_ignored_modules();
    ctx.config = config;

    return true;
}

// ---- Core run loop ----------------------------------------------------------

namespace {
// Dump the validated module configurations and manifests as JSON files into dump_path.
void dump_config_and_manifests(const Everest::ManagerConfig& config, const fs::path& dump_path) {
    EVLOG_debug << fmt::format("Dumping validated config and manifests into '{}'", dump_path.string());

    const auto config_dump_path = dump_path / "config.json";
    std::ofstream output_config_stream(config_dump_path);
    output_config_stream << json(config.get_module_configurations()).dump(DUMP_INDENT);

    const auto manifests = config.get_manifests();
    for (const auto& module : manifests.items()) {
        const std::string filename = module.key() + ".json";
        const auto module_output_path = dump_path / filename;
        std::ofstream output_stream(module_output_path);

        output_stream << module.value().dump(DUMP_INDENT);
    }
}
} // namespace

int Manager::run() {
    const bool check = (m_vm.count("check") != 0);
    auto status_fifo = StatusFifo::create_from_path(m_vm["status-fifo"].as<std::string>());
    m_status_fifo = &status_fifo;
    const bool boot_into_idle = m_vm.count("into-idle") != 0;
    const bool cfg_api_active = m_vm.count("configuration-api") != 0;
    const bool lfc_api_active = m_vm.count("lifecycle-api") != 0;
    m_sigint_received = false;
    m_shutdown_cause = ShutdownCause::None;
    transition_to(ManagerState::Initializing);
    m_unexpected_module_exit_count = 0;
    m_shutdown_start_time = std::nullopt;
    m_force_terminate_start_time = std::nullopt;
    m_force_kill_sent = false;
    auto signal_polling = system::SignalPolling();

    const auto prefix_opt = parse_string_option(m_vm, "prefix");
    const auto config_opt = parse_string_option(m_vm, "config");
    const auto conf_opt = parse_string_option(m_vm, "conf");
    const auto db_opt = parse_string_option(m_vm, "db");
    const bool reset_from_yaml = (m_vm.count("reset-from-yaml") != 0);

    // --conf is a deprecated alias for --config; using both at once is ambiguous, so reject it.
    if (m_vm.count("conf") != 0) {
        EVLOG_warning << "The '--conf' option is deprecated and will be removed in a future version. Please use "
                         "'--config' instead.";
    }
    if (m_vm.count("config") != 0 && m_vm.count("conf") != 0) {
        throw BootException("--config and --conf are mutually exclusive; --conf is a deprecated alias for --config.");
    }
    // Resolve the deprecated alias: fall back to --conf when --config is not given.
    const auto config_path = config_opt.empty() ? conf_opt : config_opt;

    const bool have_config = not config_path.empty();
    if (reset_from_yaml && not have_config) {
        throw BootException("--reset-from-yaml requires --config; there is no YAML to re-seed from.");
    }

    // Without --config the manager runs on built-in defaults; module configs then come from the
    // database (seeded empty on a fresh database, so the manager boots into Idle).
    ManagerSettings ms = have_config ? ManagerSettings(prefix_opt, config_path, db_opt)
                                     : ManagerSettings(ManagerSettings::WithoutConfig{}, prefix_opt, db_opt);

    // CLI override for mqtt_everest_prefix (e.g. for parallel test execution).
    if (m_vm.count("mqtt_everest_prefix") != 0) {
        auto prefix = m_vm["mqtt_everest_prefix"].as<std::string>();
        if (!prefix.empty() && prefix.back() != '/') {
            prefix += "/";
        }
        ms.mqtt_settings.everest_prefix = prefix;
    }

    Logging::init(ms.runtime_settings.logging_config_file.string());

    Date::preload_tzdb();

    print_start_message(ms.version_information);

    if (not ms.mqtt_settings.uses_socket()) {
        EVLOG_info << "Using MQTT broker " << ms.mqtt_settings.broker_host << ":" << ms.mqtt_settings.broker_port;
    } else {
        EVLOG_info << "Using MQTT broker unix domain sockets:" << ms.mqtt_settings.broker_socket_path;
    }
    if (ms.runtime_settings.telemetry_enabled) {
        EVLOG_info << "Telemetry enabled";
    }
    if (not ms.run_as_user.empty()) {
        EVLOG_info << "EVerest will run as system user: " << ms.run_as_user;
    }
    if (ms.runtime_settings.forward_exceptions) {
        EVLOG_info << "Catching and forwarding command exceptions to callers";
    }

    auto admin_panel = ManagerAdminPanel::create(ms);

    EVLOG_verbose << fmt::format("EVerest prefix was set to {}", ms.runtime_settings.prefix.string());

    // dump all manifests if requested and terminate afterwards
    if (m_vm.count("dumpmanifests")) {
        const auto dumpmanifests_path = fs::path(m_vm["dumpmanifests"].as<std::string>());
        EVLOG_debug << fmt::format("Dumping all known validated manifests into '{}'", dumpmanifests_path.string());

        auto manifests = Config::load_all_manifests(ms.runtime_settings.modules_dir.string(), ms.schemas_dir.string());

        for (const auto& module : manifests.items()) {
            const std::string filename = module.key() + ".yaml";
            const auto module_output_path = dumpmanifests_path / filename;
            // FIXME (aw): should we check if the directory exists?
            std::ofstream output_stream(module_output_path);

            // FIXME (aw): this should be either YAML prettyfied, or better, directly copied
            output_stream << module.value().dump(DUMP_INDENT);
        }

        return EXIT_SUCCESS;
    }

    // --check validates the YAML given via --config and exits. It must run before any database
    // access: checking a config is a read-only diagnostic and must neither seed the database nor
    // be overridden by an existing database slot (nor require an MQTT broker).
    if (check) {
        if (not have_config) {
            EVLOG_error << "--check requires --config; there is no YAML to validate.";
            return EXIT_FAILURE;
        }
        try {
            const ManagerConfig validated_config(ms);
            if (m_vm.count("dump")) {
                dump_config_and_manifests(validated_config, fs::path(m_vm["dump"].as<std::string>()));
            }
        } catch (const std::exception& e) {
            EVLOG_error << "Config is invalid: " << e.what();
            return EXIT_FAILURE;
        }
        EVLOG_info << "Config is valid, terminating as requested";
        return EXIT_SUCCESS;
    }

    {
        auto bs = init_database_bootstrap(ms, reset_from_yaml);
        m_db_connection = std::move(bs.db_connection);
        if (not bs.module_configs_initialized) {
            // no valid database entry AND it's impossible to write one
            // it would be brave to continue here
            EVLOG_critical << "Couldn't initialize the configuration database!";
            return EXIT_FAILURE;
        }
    }

    m_config_service_core = std::make_unique<config::ConfigServiceCore>(ms, m_db_connection);

    std::unique_ptr<MQTTAbstraction> mqtt_abstraction;
    if (lfc_api_active) {
        LwtCfg lwt_cfg;
        lwt_cfg.topic = Everest::api::lifecycle::LifecycleAPI::Lwt::get_topic();
        lwt_cfg.data = Everest::api::lifecycle::LifecycleAPI::Lwt::get_data();
        mqtt_abstraction = create_and_connect_mqtt(ms, std::optional<LwtCfg>{lwt_cfg});
    } else {
        mqtt_abstraction = create_and_connect_mqtt(ms, std::nullopt);
    }
    if (!mqtt_abstraction) {
        return EXIT_FAILURE;
    }

    const bool retain_topics = (m_vm.count("retain-topics") != 0);

    std::shared_ptr<const Everest::ManagerConfig> config;
    std::vector<std::string> standalone_modules;
    std::vector<std::string> ignored_modules;

    RuntimeContext runtime_ctx{config, *mqtt_abstraction, ignored_modules, standalone_modules,
                               ms,     status_fifo,       retain_topics};

    bool runtime_ctx_has_valid_config = reload_and_update_context(runtime_ctx);

    if (m_vm.count("dump")) {
        if (not runtime_ctx_has_valid_config) {
            // runtime_ctx.config is null in this case; there is nothing to dump.
            EVLOG_error << "Cannot dump config: no valid module configuration is available.";
            return EXIT_FAILURE;
        }
        dump_config_and_manifests(*runtime_ctx.config, fs::path(m_vm["dump"].as<std::string>()));
    }

    auto config_service = std::make_unique<config::MqttConfigServiceHandler>(*mqtt_abstraction, *m_config_service_core);

    register_state_transition_handler([this](ManagerState from, ManagerState to) {
        if (to == ManagerState::Running) {
            m_config_service_core->set_modules_running();
        } else if (from == ManagerState::Running) {
            m_config_service_core->set_modules_stopping();
            // we consider ShutdownFinalizing to be "stopped": this allows ConfigServiceCore to consider the modules
            // stopped and allow to process reinitilization
        } else if (to == ManagerState::ShutdownFinalizing) {
            m_config_service_core->set_modules_stopped();
        } else if (to == ManagerState::StartingModules) {
            m_config_service_core->set_modules_starting();
        }
    });

    if (not boot_into_idle and runtime_ctx_has_valid_config) {
        m_module_handles = handle_start_modules(runtime_ctx);
    } else {
        if (boot_into_idle) {
            EVLOG_info << "Requested by command-line-parameter -> entering Idle";
        } else if (not runtime_ctx_has_valid_config) {
            EVLOG_error << "No valid module configuration found -> entering Idle";
        }
        transition_to(ManagerState::Idle);
    }

    if (const auto err_set_user = ManagerAdminPanel::switch_manager_user_if_needed(runtime_ctx.ms)) {
        EVLOG_error << "Error switching manager to user " << runtime_ctx.ms.run_as_user << ": " << *err_set_user;
        return EXIT_FAILURE;
    }

    int wstatus; // NOLINT(cppcoreguidelines-init-variables): this is always initialized in the following waitpid call
    m_shutdown_info.clear();

    while (true) {
        if (handle_waitpid_event(wstatus, runtime_ctx, admin_panel)) {
            continue;
        }

        const auto lifecycle_advance = advance_lifecycle_state_if_ready(runtime_ctx, admin_panel);
        if (lifecycle_advance.status == LifecycleAdvanceResult::Status::ExitRequested) {
            return *lifecycle_advance.exit_code;
        }
        if (lifecycle_advance.status == LifecycleAdvanceResult::Status::TransitionApplied) {
            continue;
        }

        if (const auto exit_from_panel = handle_controller_ipc_poll(runtime_ctx, admin_panel, prefix_opt)) {
            return *exit_from_panel;
        }
        if (const auto exit_from_signal = handle_signal_poll(signal_polling, runtime_ctx, admin_panel)) {
            return *exit_from_signal;
        }

        handle_shutdown_timeout(runtime_ctx);
    }

    return EXIT_SUCCESS;
}

// ---- Setup/helpers ----------------------------------------------------------

std::string_view Manager::state_to_string(ManagerState state) const {
    switch (state) {
    case ManagerState::Initializing:
        return "Initializing";
    case ManagerState::StartingModules:
        return "StartingModules";
    case ManagerState::Running:
        return "Running";
    case ManagerState::RestartRequested:
        return "RestartRequested";
    case ManagerState::CrashShutdownInProgress:
        return "CrashShutdownInProgress";
    case ManagerState::ShutdownRequested:
        return "ShutdownRequested";
    case ManagerState::ForceTerminating:
        return "ForceTerminating";
    case ManagerState::ShutdownFinalizing:
        return "ShutdownFinalizing";
    case ManagerState::Idle:
        return "Idle";
    case ManagerState::Exiting:
        return "Exiting";
    default:
        return "Unknown";
    }
}

std::shared_ptr<const ManagerConfig>
Manager::load_and_validate_config(const ManagerSettings& ms,
                                  everest::config::ModuleConfigurations& preloaded_module_configs) const {
    const auto start_time = std::chrono::steady_clock::now();
    std::shared_ptr<const ManagerConfig> config;
    try {
        config = std::make_shared<const ManagerConfig>(ms, std::move(preloaded_module_configs));
    } catch (EverestInternalError& e) {
        EVLOG_error << fmt::format("Failed to load and validate config!\n{}", boost::diagnostic_information(e, true));
        throw;
    } catch (boost::exception& e) {
        EVLOG_error << "Failed to load and validate config!";
        EVLOG_critical << fmt::format("Caught top level boost::exception:\n{}", boost::diagnostic_information(e, true));
        throw;
    } catch (std::exception& e) {
        EVLOG_error << "Failed to load and validate config!";
        EVLOG_critical << fmt::format("Caught top level std::exception:\n{}", boost::diagnostic_information(e, true));
        throw;
    }
    const auto end_time = std::chrono::steady_clock::now();
    EVLOG_info << "Config loading completed in "
               << std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count() << "ms";
    return config;
}

std::unique_ptr<MQTTAbstraction> Manager::create_and_connect_mqtt(const ManagerSettings& ms) const {
    auto mqtt_abstraction = make_mqtt_abstraction(ms.mqtt_settings);
    if (!mqtt_abstraction->connect()) {
        if (not ms.mqtt_settings.uses_socket()) {
            EVLOG_error << fmt::format("Cannot connect to MQTT broker at {}:{}", ms.mqtt_settings.broker_host,
                                       ms.mqtt_settings.broker_port);
        } else {
            EVLOG_error << fmt::format("Cannot connect to MQTT broker socket at {}",
                                       ms.mqtt_settings.broker_socket_path);
        }
        return nullptr;
    }

    mqtt_abstraction->spawn_main_loop_thread();
    return mqtt_abstraction;
}

std::vector<std::string> Manager::collect_standalone_modules(const ManagerConfig& config) const {
    std::vector<std::string> standalone_modules;
    const auto& module_configurations = config.get_module_configurations();

    if (m_vm.count("standalone")) {
        // Make sure to only list existing modules and each only once
        for (const auto& module_id : m_vm["standalone"].as<std::vector<std::string>>()) {
            if (module_configurations.find(module_id) != module_configurations.end() &&
                std::find(standalone_modules.begin(), standalone_modules.end(), module_id) ==
                    standalone_modules.end()) {
                standalone_modules.push_back(module_id);
            }
        }
    }

    for (const auto& [module_id, module_config] : module_configurations) {
        if (!module_config.standalone) {
            continue;
        }
        if (std::find(standalone_modules.begin(), standalone_modules.end(), module_id) == standalone_modules.end()) {
            EVLOG_info << "Module " << fmt::format(TERMINAL_STYLE_BLUE, "{}", module_id)
                       << " marked as standalone in config";
            standalone_modules.push_back(module_id);
        }
    }

    return standalone_modules;
}

std::vector<std::string> Manager::collect_ignored_modules() const {
    if (m_vm.count("ignore")) {
        return m_vm["ignore"].as<std::vector<std::string>>();
    }
    return {};
}

void Manager::notify_status_fifo(const std::string_view message) {
    if (m_status_fifo != nullptr) {
        m_status_fifo->update(std::string(message));
    }
}

void Manager::notify_status_fifo_for_state(ManagerState state) {
    switch (state) {
    case ManagerState::Initializing:
        notify_status_fifo(StatusFifo::MANAGER_INITIALIZING);
        break;
    case ManagerState::StartingModules:
        notify_status_fifo(StatusFifo::MANAGER_STARTING_MODULES);
        break;
    case ManagerState::Running:
        notify_status_fifo(StatusFifo::MANAGER_RUNNING);
        break;
    case ManagerState::RestartRequested:
        notify_status_fifo(StatusFifo::MANAGER_RESTART_REQUESTED);
        break;
    case ManagerState::CrashShutdownInProgress:
        notify_status_fifo(StatusFifo::MANAGER_CRASH_SHUTDOWN_IN_PROGRESS);
        break;
    case ManagerState::ShutdownRequested:
        notify_status_fifo(StatusFifo::MANAGER_SHUTDOWN_REQUESTED);
        break;
    case ManagerState::ForceTerminating:
        notify_status_fifo(StatusFifo::MANAGER_FORCE_TERMINATING);
        break;
    case ManagerState::ShutdownFinalizing:
        notify_status_fifo(StatusFifo::MANAGER_SHUTDOWN_FINALIZING);
        break;
    case ManagerState::Idle:
        notify_status_fifo(StatusFifo::MANAGER_IDLE);
        break;
    case ManagerState::Exiting:
        notify_status_fifo(StatusFifo::MANAGER_EXITING);
        break;
    default:
        break;
    }
}

void Manager::notify_crash_recovery_attempt(const std::uint8_t attempt, const std::uint8_t max) {
    notify_status_fifo(fmt::format("CRASH_RECOVERY_ATTEMPT:{}/{}\n", attempt, max));
}

void Manager::transition_to_unlocked(ManagerState new_state) {
    const auto current_state = m_state.load();
    if (current_state == new_state) {
        return;
    }
    EVLOG_info << "Manager state transition: " << state_to_string(current_state) << " -> "
               << state_to_string(new_state);
    ManagerState old_state = m_state;
    m_state = new_state;
    for (const auto& handler : m_state_transition_handlers) {
        handler(old_state, new_state);
    }
    notify_status_fifo_for_state(new_state);
}

void Manager::transition_to(ManagerState new_state) {
    const std::lock_guard<std::mutex> lock(m_state_transition_mutex);
    transition_to_unlocked(new_state);
}

ManagerState Manager::current_state_unlocked() const {
    return m_state.load();
}

bool Manager::is_in_shutdown_flow_state_unlocked() const {
    const auto s = current_state_unlocked();
    return (s == ManagerState::ShutdownRequested) || (s == ManagerState::CrashShutdownInProgress) ||
           (s == ManagerState::ForceTerminating) || (s == ManagerState::RestartRequested) ||
           (s == ManagerState::ShutdownFinalizing);
}

void Manager::register_state_transition_handler(std::function<void(ManagerState, ManagerState)> handler) {
    m_state_transition_handlers.push_back(std::move(handler));
}

Manager::Manager(const po::variables_map& vm) :
    m_vm(vm),
    m_recover_module_crashes(vm.count("recover-module-crashes") != 0),
    m_graceful_shutdown_enabled(vm.count("graceful-shutdown") != 0) {
}

// ---- State predicates -------------------------------------------------------

bool Manager::is_in_shutdown_flow_state() const {
    const std::lock_guard<std::mutex> lock(m_state_transition_mutex);
    return is_in_shutdown_flow_state_unlocked();
}

bool Manager::is_restart_requested() const {
    const std::lock_guard<std::mutex> lock(m_state_transition_mutex);
    return current_state_unlocked() == ManagerState::RestartRequested;
}

bool Manager::are_modules_started() const {
    const std::lock_guard<std::mutex> lock(m_state_transition_mutex);
    return current_state_unlocked() == ManagerState::Running;
}

bool Manager::is_idle() const {
    const std::lock_guard<std::mutex> lock(m_state_transition_mutex);
    return current_state_unlocked() == ManagerState::Idle;
}

// ---- Event loop dispatch handlers ------------------------------------------

/// \brief Handle module startup by publishing metadata, registering handlers, and spawning module processes.
std::map<pid_t, std::string> Manager::handle_start_modules(const RuntimeContext& ctx) {
    BOOST_LOG_FUNCTION();
    m_module_startup_start_time = std::chrono::steady_clock::now();
    auto& config = *ctx.config;
    const auto& module_configurations = config.get_module_configurations();
    if (module_configurations.size() == 0) {
        EVLOG_info << "List of modules to start is empty";
        transition_to(ManagerState::Idle);
        return {};
    }
    transition_to(ManagerState::StartingModules);
    auto& mqtt_abstraction = ctx.mqtt_abstraction;
    const auto& ignored_modules = ctx.ignored_modules;
    const auto& standalone_modules = ctx.standalone_modules;
    const auto& ms = ctx.ms;
    auto& status_fifo = ctx.status_fifo;
    const bool retain_topics = ctx.retain_topics;

    std::vector<ModuleStartInfo> modules_to_spawn;

    modules_to_spawn.reserve(module_configurations.size());
    const auto number_of_modules = module_configurations.size();
    EVLOG_info << "Starting " << number_of_modules << " modules";

    publish_startup_metadata(ctx);

    for (const auto& [module_id_, module_config] : module_configurations) {
        const auto& module_name = module_config.module_name;
        const auto& module_id = module_id_;
        if (std::any_of(ignored_modules.begin(), ignored_modules.end(),
                        [module_id](const auto& element) { return element == module_id; })) {
            EVLOG_info << fmt::format("Ignoring module: {}", module_id);
            continue;
        }

        // ready handlers registered in earlier loop iterations may already fire on the
        // message-dispatch thread and iterate m_modules_ready, so structural changes need the lock
        auto module_it = [&] {
            const std::lock_guard<std::mutex> lock(m_modules_ready_mutex);
            return m_modules_ready.emplace(module_id, ModuleReadyInfo{}).first;
        }();

        std::vector<std::string> capabilities =
            module_configurations.at(module_id).capabilities.value_or(std::vector<std::string>{});

        if (not capabilities.empty()) {
            EVLOG_info << fmt::format("Module {} wants to acquire the following capabilities: {}", module_name,
                                      fmt::join(capabilities.begin(), capabilities.end(), " "));
        }

        const Handler module_ready_handler = [this, module_id, &mqtt_abstraction, standalone_modules,
                                              mqtt_everest_prefix = ms.mqtt_settings.everest_prefix, &status_fifo,
                                              retain_topics](const std::string&, const nlohmann::json& json) {
            EVLOG_debug << fmt::format("received module ready signal for module: {}({})", module_id, json.dump());
            bool all_modules_ready = false;
            std::size_t modules_spawned = 0;
            const std::size_t modules_ready_count = [&] {
                const std::lock_guard<std::mutex> lock(m_modules_ready_mutex);
                // FIXME (aw): here are race conditions, if the ready handler gets called while modules are shut down!
                try {
                    m_modules_ready.at(module_id).ready = json.get<bool>();
                } catch (const std::out_of_range& ex) {
                    // This can happen if we're shutting down and a module becomes
                    // ready.
                    EVLOG_error << "The module " << module_id << " is not in `modules_ready`: " << ex.what();
                    return std::size_t{0};
                }
                for (const auto& mod : m_modules_ready) {
                    const std::string text_ready =
                        fmt::format((mod.second.ready) ? TERMINAL_STYLE_OK : TERMINAL_STYLE_ERROR, "ready");
                    EVLOG_debug << fmt::format("  {}: {}", mod.first, text_ready);
                    if (mod.second.ready) {
                        modules_spawned += 1;
                    }
                }
                if (!standalone_modules.empty() && std::find(standalone_modules.begin(), standalone_modules.end(),
                                                             module_id) != standalone_modules.end()) {
                    EVLOG_info << fmt::format("Standalone module {} initialized.", module_id);
                }
                all_modules_ready = std::all_of(m_modules_ready.begin(), m_modules_ready.end(),
                                                [](const auto& element) { return element.second.ready; });
                return m_modules_ready.size();
            }();

            if (all_modules_ready) {
                const auto complete_end_time = std::chrono::steady_clock::now();
                if (not retain_topics) {
                    EVLOG_info << "Clearing retained topics published by manager during startup";
                    mqtt_abstraction.clear_retained_topics();
                } else {
                    EVLOG_info << "Keeping retained topics published by manager during startup for inspection";
                }
                EVLOG_info << fmt::format(TERMINAL_STYLE_OK,
                                          "🚙🚙🚙 All modules are initialized. EVerest up and running [{}ms] 🚙🚙🚙",
                                          std::chrono::duration_cast<std::chrono::milliseconds>(
                                              complete_end_time - m_module_startup_start_time)
                                              .count());

                bool skip_running_transition = false;
                {
                    const std::lock_guard<std::mutex> state_lock(m_state_transition_mutex);
                    if (m_sigint_received || is_in_shutdown_flow_state_unlocked()) {
                        EVLOG_info << "All modules reported ready while shutdown is already in progress. "
                                      "Skipping transition to Running.";
                        skip_running_transition = true;
                    } else {
                        transition_to_unlocked(ManagerState::Running);
                    }
                }
                if (skip_running_transition) {
                    return;
                }

                status_fifo.update(StatusFifo::ALL_MODULES_STARTED);
                MqttMessagePayload payload{MqttMessageType::GlobalReady, nlohmann::json(true)};

                mqtt_abstraction.publish(fmt::format("{}ready", mqtt_everest_prefix), payload);
            } else if (!standalone_modules.empty()) {
                if (modules_spawned == modules_ready_count - standalone_modules.size()) {
                    EVLOG_info << fmt::format(fg(fmt::terminal_color::green),
                                              "Modules started by manager are ready, waiting for standalone modules.");
                    status_fifo.update(StatusFifo::WAITING_FOR_STANDALONE_MODULES);
                }
            }
        };

        const std::string ready_topic = fmt::format("{}/ready", config.mqtt_module_prefix(module_id));
        auto ready_token =
            std::make_shared<TypedHandler>(HandlerType::ModuleReady, std::make_shared<Handler>(module_ready_handler));
        {
            const std::lock_guard<std::mutex> lock(m_modules_ready_mutex);
            module_it->second.ready_token = ready_token;
        }
        mqtt_abstraction.register_handler(ready_topic, ready_token, QOS::QOS2);

        if (std::any_of(standalone_modules.begin(), standalone_modules.end(),
                        [module_id](const auto& element) { return element == module_id; })) {
            EVLOG_info << "Not starting standalone module: " << fmt::format(TERMINAL_STYLE_BLUE, "{}", module_id);
            continue;
        }

        const std::string binary_filename = fmt::format("{}", module_name);
        const std::string javascript_library_filename = "index.js";
        const std::string python_filename = "module.py";
        const auto module_path = ms.runtime_settings.modules_dir / module_name;
        const auto printable_module_name = config.printable_identifier(module_id);
        const auto binary_path = module_path / binary_filename;
        const auto javascript_library_path = module_path / javascript_library_filename;
        const auto python_module_path = module_path / python_filename;

        if (fs::exists(binary_path)) {
            EVLOG_debug << fmt::format("module: {} ({}) provided as binary", module_id, module_name);
            modules_to_spawn.emplace_back(module_id, printable_module_name, ModuleStartInfo::Language::cpp, binary_path,
                                          capabilities);
        } else if (fs::exists(javascript_library_path)) {
            EVLOG_debug << fmt::format("module: {} ({}) provided as javascript library", module_id, module_name);
            modules_to_spawn.emplace_back(module_id, printable_module_name, ModuleStartInfo::Language::javascript,
                                          fs::canonical(javascript_library_path), capabilities);
        } else if (fs::exists(python_module_path)) {
            EVLOG_verbose << fmt::format("module: {} ({}) provided as python module", module_id, module_name);
            modules_to_spawn.emplace_back(module_id, printable_module_name, ModuleStartInfo::Language::python,
                                          fs::canonical(python_module_path), capabilities);
        } else {
            if (module_id == "probe" || module_name == "ProbeModule") {
                EVLOG_error << "You are trying to start the probe module as binary, please check "
                               "your test case, did you add \"@pytest.mark.probe_module\" to your test case?";
            }
            throw std::runtime_error(
                fmt::format("module: {} ({}) cannot be loaded because no Binary, JavaScript or Python "
                            "module has been found\n"
                            "  checked paths:\n"
                            "    binary: {}\n"
                            "    js:  {}\n"
                            "    py:  {}\n",
                            module_id, module_name, binary_path.string(), javascript_library_path.string(),
                            python_module_path.string()));
        }
    }

    return spawn_modules(modules_to_spawn, ms);
}

Manager::LifecycleAdvanceResult Manager::advance_lifecycle_state_if_ready(RuntimeContext& ctx,
                                                                          ManagerAdminPanel& admin_panel) {
    const bool in_shutdown_flow = is_in_shutdown_flow_state();
    const bool crash_in_progress = (m_shutdown_cause == ShutdownCause::Crash);
    const bool restart_requested = (m_shutdown_cause == ShutdownCause::Restart);

    // Finalize shutdown as soon as all module processes are gone, even if we got here through ECHILD
    // after a timeout-triggered force shutdown.
    if (in_shutdown_flow && m_module_handles.empty()) {
        {
            const std::lock_guard<std::mutex> lock(m_state_transition_mutex);
            if (current_state_unlocked() != ManagerState::ShutdownFinalizing) {
                transition_to_unlocked(ManagerState::ShutdownFinalizing);
            }
        }
        // A SIGINT/SIGTERM during a crash drain means the user wants to stop; do not auto-restart.
        if (crash_in_progress && m_recover_module_crashes && !m_sigint_received &&
            m_unexpected_module_exit_count <= MAX_UNEXPECTED_MODULE_RESTARTS) {
            EVLOG_warning << fmt::format(
                "Unexpected module exit recovery attempt {}/{}. Reloading config and restarting "
                "modules.",
                m_unexpected_module_exit_count, MAX_UNEXPECTED_MODULE_RESTARTS);
            notify_crash_recovery_attempt(m_unexpected_module_exit_count, MAX_UNEXPECTED_MODULE_RESTARTS);
            handle_restart_modules_after_shutdown(ctx);
            return {LifecycleAdvanceResult::Status::TransitionApplied, std::nullopt};
        }
        if (crash_in_progress && m_unexpected_module_exit_count > MAX_UNEXPECTED_MODULE_RESTARTS) {
            EVLOG_error << fmt::format("Reached maximum unexpected module exit recovery attempts ({}/{}). "
                                       "Manager will stay idle after shutdown.",
                                       m_unexpected_module_exit_count, MAX_UNEXPECTED_MODULE_RESTARTS);
            notify_status_fifo(StatusFifo::CRASH_RECOVERY_EXHAUSTED);
        }
        if (const auto exit_code =
                handle_finalize_shutdown_transition(ctx, admin_panel, restart_requested, crash_in_progress)) {
            return {LifecycleAdvanceResult::Status::ExitRequested, *exit_code};
        }
        return {LifecycleAdvanceResult::Status::TransitionApplied, std::nullopt};
    }

    // Admin restart can mark restart_modules while modules are still draining.
    // If all children are gone, restart immediately with reloaded config.
    if (restart_requested && m_module_handles.empty()) {
        handle_restart_modules_after_shutdown(ctx);
        return {LifecycleAdvanceResult::Status::TransitionApplied, std::nullopt};
    }

    return {LifecycleAdvanceResult::Status::NoTransition, std::nullopt};
}

bool Manager::handle_child_exit(pid_t pid, int wstatus, RuntimeContext& ctx, ManagerAdminPanel& admin_panel) {
    auto module_exited_time = std::chrono::steady_clock::now();
    if (admin_panel.is_controller_process(pid)) {
        // During intentional manager shutdown/restart, controller exit is expected.
        if (is_in_shutdown_flow_state() || m_state == ManagerState::Exiting || m_sigint_received ||
            is_restart_requested()) {
            EVLOG_info << "Controller process exited during manager shutdown/restart.";
            return true;
        }
        admin_panel.throw_if_controller_exited(pid);
    }
    const std::string wait_status = format_wait_status(wstatus);

    const auto module_iter = m_module_handles.find(pid);
    if (module_iter == m_module_handles.end()) {
        throw std::runtime_error(fmt::format("Unknown child with pid ({}) died.", pid));
    }

    const auto module_name = module_iter->second;
    m_module_handles.erase(module_iter);

    const bool unexpected_exit_during_start_or_run = [&] {
        const std::lock_guard<std::mutex> lock(m_state_transition_mutex);
        const auto s = current_state_unlocked();
        return s == ManagerState::StartingModules || s == ManagerState::Running;
    }();
    if (unexpected_exit_during_start_or_run) {
        // During startup/running, an exiting module is unexpected: trigger graceful shutdown.
        m_shutdown_cause = ShutdownCause::Crash;
        ++m_unexpected_module_exit_count;
        const auto shutdown_info_log =
            "Module " + fmt::format(TERMINAL_STYLE_BLUE, "{}", module_name) +
            (m_graceful_shutdown_enabled
                 ? " exited unexpectedly, signaling remaining modules to shut down gracefully..."
                 : " exited unexpectedly, terminating remaining modules...");
        handle_initiate_graceful_shutdown(module_exited_time, true, shutdown_info_log, ctx.mqtt_abstraction, ctx.ms);
        transition_to(ManagerState::CrashShutdownInProgress);
        m_shutdown_info.push_back({module_name, wstatus});
        return true;
    }

    if (is_in_shutdown_flow_state() || m_sigint_received) {
        // During shutdown drain, keep collecting statuses for final shutdown summary.
        EVLOG_info << "Module " << fmt::format(TERMINAL_STYLE_BLUE, "{}", module_name) << " (pid " << pid
                   << ") shutdown ["
                   << std::chrono::duration_cast<std::chrono::milliseconds>(
                          module_exited_time - m_shutdown_start_time.value_or(module_exited_time))
                          .count()
                   << "ms] with status: " << wait_status;
        m_shutdown_info.push_back({module_name, wstatus});
        return true;
    }

    EVLOG_info << fmt::format("Module {} (pid: {}) exited with status: {}.", module_name, pid, wait_status);
    return false;
}

std::optional<int> Manager::handle_signal(int signo, RuntimeContext& ctx, ManagerAdminPanel& admin_panel) {
    if (signo != SIGINT && signo != SIGTERM) {
        return std::nullopt;
    }
    if (!m_sigint_received) {
        EVLOG_info << "SIGINT/SIGTERM received";
        m_sigint_received = true;
        notify_status_fifo(StatusFifo::SIGINT_RECEIVED);
        // Do not downgrade an in-progress crash drain to a normal shutdown: finalization keys the
        // exit code off m_shutdown_cause, and a crash must stay visible as EXIT_FAILURE.
        if (m_shutdown_cause != ShutdownCause::Crash) {
            m_shutdown_cause = ShutdownCause::Normal;
        }
        m_shutdown_start_time = std::chrono::steady_clock::now();
        m_force_terminate_start_time = std::nullopt;
        m_force_kill_sent = false;
        if (m_module_handles.empty()) {
            print_shutdown_message(m_shutdown_start_time);
            admin_panel.shutdown_controller();
            disconnect_mqtt(ctx.mqtt_abstraction);
            transition_to(ManagerState::Exiting);
            return EXIT_SUCCESS;
        }
        transition_to(ManagerState::ShutdownRequested);
        EVLOG_info << "Shutting down modules...";
        if (m_graceful_shutdown_enabled) {
            ctx.mqtt_abstraction.publish(fmt::format("{}shutdown", ctx.ms.mqtt_settings.everest_prefix),
                                         std::string("true"), QOS::QOS2, false);
        }
        return std::nullopt;
    }

    EVLOG_info << "Terminating manager";
    admin_panel.shutdown_controller();
    transition_to(ManagerState::Exiting);
    return EXIT_FAILURE;
}

void Manager::handle_shutdown_timeout(RuntimeContext& ctx) {
    if (m_state == ManagerState::ShutdownFinalizing) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const bool should_check_shutdown_timeout =
        (m_state == ManagerState::ShutdownRequested || m_state == ManagerState::CrashShutdownInProgress ||
         m_state == ManagerState::RestartRequested) &&
        m_shutdown_start_time.has_value();

    // Without --graceful-shutdown there is no drain phase: force-terminate as soon as the
    // shutdown flow starts (legacy behavior, no FORCE_SHUTDOWN_TIMEOUT event).
    const auto shutdown_timeout_ms = m_graceful_shutdown_enabled ? SHUTDOWN_TIMEOUT_MS : 0;
    if (should_check_shutdown_timeout &&
        now >= m_shutdown_start_time.value() + std::chrono::milliseconds(shutdown_timeout_ms)) {
        transition_to(ManagerState::ForceTerminating);
        if (m_graceful_shutdown_enabled) {
            notify_status_fifo(StatusFifo::FORCE_SHUTDOWN_TIMEOUT);
            EVLOG_warning << "Not all modules shut down within the timeout. Forcefully terminating remaining modules.";
        } else {
            EVLOG_info << "Terminating modules (graceful shutdown not enabled)...";
        }
        shutdown_modules(m_module_handles, *ctx.config, ctx.mqtt_abstraction);
        m_force_terminate_start_time = now;
        m_force_kill_sent = false;
        return;
    }

    if (m_state != ManagerState::ForceTerminating || m_force_kill_sent || !m_force_terminate_start_time.has_value() ||
        m_module_handles.empty()) {
        return;
    }

    if (now < m_force_terminate_start_time.value() + std::chrono::milliseconds(FORCE_KILL_GRACE_TIMEOUT_MS)) {
        return;
    }

    EVLOG_warning << fmt::format(
        "Modules still alive {}ms after SIGTERM in ForceTerminating. Escalating to SIGKILL for remaining {} modules.",
        FORCE_KILL_GRACE_TIMEOUT_MS, m_module_handles.size());
    for (const auto& child : m_module_handles) {
        const auto retval = kill(child.first, SIGKILL);
        if (retval != 0) {
            EVLOG_critical << fmt::format("SIGKILL of child: {} (pid: {}) {}: {}.", child.second, child.first,
                                          fmt::format(TERMINAL_STYLE_ERROR, "failed"), retval);
        } else {
            EVLOG_info << fmt::format("SIGKILL of child: {} (pid: {}) {}.", child.second, child.first,
                                      fmt::format(TERMINAL_STYLE_OK, "succeeded"));
        }
    }
    m_force_kill_sent = true;
}

bool Manager::handle_waitpid_event(int& wstatus, RuntimeContext& ctx, ManagerAdminPanel& admin_panel) {
    // non-blocking as this main loop also processes controller RPC and the signal fd
    const auto pid = waitpid(-1, &wstatus, WNOHANG);
    if (pid == 0) {
        return false;
    }
    if (pid == -1) {
        if (errno != ECHILD) {
            throw std::runtime_error(fmt::format("Syscall to waitpid() failed ({})", strerror(errno)));
        }
        // ECHILD: no more OS child processes.
        // If we still track module pids here, internal bookkeeping diverged from kernel state and
        // shutdown/restart finalization can stall forever waiting for m_module_handles to drain.
        if (!m_module_handles.empty()) {
            EVLOG_warning << fmt::format("waitpid() returned ECHILD but manager still tracks {} module pids. "
                                         "Clearing stale module handles to continue lifecycle finalization.",
                                         m_module_handles.size());
            m_module_handles.clear();
        }
        return false;
    }
    return handle_child_exit(pid, wstatus, ctx, admin_panel);
}

std::optional<int> Manager::handle_controller_ipc_poll(RuntimeContext& ctx, ManagerAdminPanel& admin_panel,
                                                       const std::string& prefix_opt) {
    bool modules_started = are_modules_started();
    const bool restart_already_requested = is_restart_requested();
    bool restart_requested = restart_already_requested;
    if (const auto exit_from_panel = admin_panel.poll_controller_ipc(restart_requested, modules_started, prefix_opt)) {
        return *exit_from_panel;
    }

    // Only act on the transition into RestartRequested; the state itself preserves the restart
    // intent for advance_lifecycle_state_if_ready() once all children have exited.
    if (restart_requested && !restart_already_requested) {
        m_shutdown_cause = ShutdownCause::Restart;
        transition_to(ManagerState::RestartRequested);
        if (m_graceful_shutdown_enabled) {
            ctx.mqtt_abstraction.publish(fmt::format("{}shutdown", ctx.ms.mqtt_settings.everest_prefix),
                                         std::string("true"), QOS::QOS2, false);
        }
        // Arm the graceful-shutdown deadline once so timeout/fallback handling covers modules
        // that do not exit after the MQTT shutdown publish. Re-arming on subsequent loop
        // iterations would push the force-terminate deadline back forever.
        if (!m_module_handles.empty()) {
            m_shutdown_start_time = std::chrono::steady_clock::now();
        }
    }

    return std::nullopt;
}

int Manager::signal_poll_timeout_ms() const {
    if (is_in_shutdown_flow_state()) {
        // shutdown/force-kill deadlines are checked from the main loop
        return SIGNAL_POLL_TIMEOUT_MS;
    }
    return IDLE_SIGNAL_POLL_TIMEOUT_MS;
}

std::optional<int> Manager::handle_signal_poll(system::SignalPolling& signal_polling, RuntimeContext& ctx,
                                               ManagerAdminPanel& admin_panel) {
    // a readable controller IPC socket also ends the poll, so controller requests are serviced
    // promptly on the next loop iteration even during a long idle poll
    const auto signal_received =
        signal_polling.poll_signal(signal_poll_timeout_ms(), admin_panel.controller_ipc_fd().value_or(-1));
    if (!signal_received.has_value()) {
        return std::nullopt;
    }
    return handle_signal(signal_received.value(), ctx, admin_panel);
}

int main(int argc, char* argv[]) {
    po::options_description desc("EVerest manager");
    desc.add_options()("version", "Print version and exit");
    desc.add_options()("help,h", "produce help message");
    desc.add_options()("check", "Check and validate all config files and exit (0=success)");
    desc.add_options()("dump", po::value<std::string>(),
                       "Dump validated and augmented main config and all used module manifests into dir");
    desc.add_options()("dumpmanifests", po::value<std::string>(),
                       "Dump manifests of all modules into dir (even modules not used in config) and exit");
    desc.add_options()("prefix", po::value<std::string>(), "Prefix path of everest installation");
    desc.add_options()("standalone,s", po::value<std::vector<std::string>>()->multitoken(),
                       "Module ID(s) to not automatically start child processes for (those must be started manually to "
                       "make the framework start!).");
    desc.add_options()("ignore", po::value<std::vector<std::string>>()->multitoken(),
                       "Module ID(s) to ignore: Do not automatically start child processes and do not require that "
                       "they are started.");
    desc.add_options()("dontvalidateschema", "Don't validate json schema on every message");
    desc.add_options()("config", po::value<std::string>(),
                       "Full path to a config file.  If the file does not exist and has no extension, it will be "
                       "looked up in the default config directory. Optional: if omitted, the manager boots from the "
                       "database, or seeds an empty config and boots into Idle on a fresh database.");
    desc.add_options()("conf", po::value<std::string>(), "Deprecated: Same as --config. Do not use both.");
    desc.add_options()("db", po::value<std::string>(),
                       "Full path to the configuration database file. Required. "
                       "The database is initialized automatically from active_modules in the YAML config on first run, "
                       "or with an empty config when no --config is given.");
    desc.add_options()("reset-from-yaml", "Discard the existing database slot and re-seed from the YAML config file. "
                                          "Intended for development use when you want to reset to a known YAML state. "
                                          "Requires --config.");
    desc.add_options()("into-idle", "Boot into idle state (no modules are started)");
    desc.add_options()("recover-module-crashes",
                       "After unexpected module exit, reload config and restart modules (bounded by an internal retry "
                       "limit). Default: shut down all modules and exit the manager.");
    desc.add_options()("graceful-shutdown",
                       "On shutdown/restart, publish the shutdown signal via MQTT so modules can run their shutdown "
                       "handlers, and force-terminate stragglers only after a timeout. Default: terminate module "
                       "processes immediately (SIGTERM, escalating to SIGKILL).");
    desc.add_options()("status-fifo", po::value<std::string>()->default_value(""),
                       "Path to a named pipe, that shall be used for status updates from the manager");
    desc.add_options()("retain-topics", "Retain configuration MQTT topics setup by manager for inspection, by default "
                                        "these will be cleared after startup");
    desc.add_options()("mqtt_everest_prefix", po::value<std::string>(),
                       "Override the MQTT everest prefix (useful for running multiple instances in parallel)");
    po::variables_map vm;

    try {
        const auto default_logging_cfg =
            defaults::PREFIX / fs::path(defaults::SYSCONF_DIR) / defaults::NAMESPACE / defaults::LOGGING_CONFIG_NAME;
        if (fs::exists(default_logging_cfg)) {
            Logging::init(default_logging_cfg.string());
        }
        int style = po::command_line_style::default_style & ~po::command_line_style::allow_guessing;

        po::store(po::parse_command_line(argc, argv, desc, style), vm);
        po::notify(vm);

        if (vm.count("help") != 0) {
            desc.print(std::cout);
            return EXIT_SUCCESS;
        }

        if (vm.count("version") != 0) {
            std::string argv0;
            if (argc > 0) {
                argv0 = *argv;
            }
            std::cout << argv0 << " (" << PROJECT_NAME << " " << PROJECT_VERSION << " " << GIT_VERSION << ") "
                      << std::endl;
            return EXIT_SUCCESS;
        }

        Manager manager(vm);
        return manager.run();

    } catch (const BootException& e) {
        EVLOG_error << "Failed to start up everest:\n" << e.what();
        return EXIT_FAILURE;
    } catch (const std::exception& e) {
        EVLOG_error << "Main manager process exits because of caught exception:\n" << e.what();
        return EXIT_FAILURE;
    }
}
