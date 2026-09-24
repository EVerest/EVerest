// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <signal.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include <boost/exception/diagnostic_information.hpp>
#include <boost/program_options.hpp>

#include <fmt/color.h>
#include <fmt/core.h>
#include <fmt/ranges.h>

#include <everest/logging.hpp>
#include <framework/local_bus.hpp>
#include <framework/local_module.hpp>
#include <framework/runtime.hpp>
#include <utils/config.hpp>
#include <utils/config/config_service_core.hpp>
#include <utils/config/settings.hpp>
#include <utils/config/storage_userconfig.hpp>
#include <utils/date.hpp>
#include <utils/mqtt_abstraction.hpp>
#include <utils/mqtt_config_service.hpp>

#include <generated/version_information.hpp>

#include "module_host.hpp"

namespace po = boost::program_options;

using namespace Everest;

namespace {

constexpr auto SHUTDOWN_TIMEOUT = std::chrono::seconds(5);
constexpr auto READY_RETURN_TIMEOUT = std::chrono::seconds(1);

std::shared_ptr<MQTTAbstraction> connect_bridge(const MQTTSettings& settings) {
    std::shared_ptr<MQTTAbstraction> bridge = make_mqtt_abstraction(settings);
    if (not bridge->connect()) {
        if (settings.uses_socket()) {
            EVLOG_warning << fmt::format("Cannot connect to MQTT broker socket at {}, running without external MQTT",
                                         settings.broker_socket_path);
        } else {
            EVLOG_warning << fmt::format("Cannot connect to MQTT broker at {}:{}, running without external MQTT",
                                         settings.broker_host, settings.broker_port);
        }
        return nullptr;
    }
    bridge->spawn_main_loop_thread();
    return bridge;
}

// Plain ASCII art, coloured at runtime with the same rotating palette the manager uses for its banner.
constexpr std::array<std::string_view, 6> START_BANNER{{
    R"(  ________      __                _       _   _            )",
    R"( |  ____\ \    / /               | |     | \ | |           )",
    R"( | |__   \ \  / /__ _ __ ___  ___| |_    |  \| | ___  ___  )",
    R"( |  __|   \ \/ / _ \ '__/ _ \/ __| __|   | . ` |/ _ \/ _ \ )",
    R"( | |____   \  /  __/ | |  __/\__ \ |_    | |\  |  __/ (_) |)",
    R"( |______|   \/ \___|_|  \___||___/\__|   |_| \_|\___|\___/ )",
}};
constexpr std::array<std::string_view, 6> BANNER_COLORS{{"35;95", "31;91", "33;93", "32;92", "36;96", "34;94"}};

void print_start_banner() {
    std::size_t color = 0;
    for (const auto& line : START_BANNER) {
        std::string out;
        for (const char c : line) {
            if (c == ' ') {
                out += c;
                continue;
            }
            out += fmt::format("\033[0;1;{}m{}", BANNER_COLORS.at(color % BANNER_COLORS.size()), c);
            color++;
        }
        EVLOG_info << out << "\033[0m";
    }
    EVLOG_info << "";
}

/// Blocks until SIGINT or SIGTERM; SIGUSR1 logs the bus state and keeps waiting.
int wait_for_termination_signal(const sigset_t& signals, const std::function<void()>& on_usr1) {
    const int fd = signalfd(-1, &signals, SFD_CLOEXEC);
    if (fd < 0) {
        EVLOG_error << "Cannot create signalfd, falling back to sigwait";
        int signal = 0;
        sigwait(&signals, &signal);
        return signal;
    }
    signalfd_siginfo info{};
    while (true) {
        if (read(fd, &info, sizeof(info)) != static_cast<ssize_t>(sizeof(info))) {
            if (errno == EINTR) {
                continue;
            }
            EVLOG_error << "Reading the signalfd failed, shutting down";
            info.ssi_signo = SIGTERM;
            break;
        }
        if (info.ssi_signo == SIGUSR1) {
            on_usr1();
            continue;
        }
        break;
    }
    close(fd);
    return static_cast<int>(info.ssi_signo);
}

int run(const po::variables_map& vm, const sigset_t& termination_signals) {
    const auto prefix_opt = parse_string_option(vm, "prefix");
    const auto config_opt = parse_string_option(vm, "config");

    ManagerSettings ms(prefix_opt, config_opt, "");
    if (vm.count("dontvalidateschema") != 0) {
        ms.validate_schema = false;
        ms.runtime_settings.validate_schema = false;
    }
    if (vm.count("log_config") != 0) {
        ms.runtime_settings.logging_config_file = vm["log_config"].as<std::string>();
    }
    if (vm.count("mqtt_broker_host") != 0) {
        ms.mqtt_settings.broker_host = vm["mqtt_broker_host"].as<std::string>();
        ms.mqtt_settings.broker_socket_path.clear();
    }
    if (vm.count("mqtt_broker_port") != 0) {
        ms.mqtt_settings.broker_port = vm["mqtt_broker_port"].as<std::uint16_t>();
    }

    Logging::init(ms.runtime_settings.logging_config_file.string(), "everest-neo");
    Date::preload_tzdb();

    print_start_banner();
    EVLOG_info << PROJECT_NAME << " " << PROJECT_VERSION << " " << GIT_VERSION << " (everest-neo)";
    EVLOG_info << "";

    auto bootstrap = init_database_bootstrap(ms);
    if (not bootstrap.module_configs_initialized) {
        EVLOG_critical << "Couldn't initialize the configuration database!";
        return EXIT_FAILURE;
    }

    std::unique_ptr<everest::config::StorageInterface> persistence_mirror;
    if (not ms.config_file.empty()) {
        persistence_mirror = std::make_unique<everest::config::UserConfigStorage>(
            ms.config_file.parent_path() / "user-config" / ms.config_file.filename());
    }
    Everest::config::ConfigServiceCore core(ms, bootstrap.db_connection, std::move(persistence_mirror));

    const auto config_start = std::chrono::steady_clock::now();
    auto module_configs = *core.get_active_module_configurations();
    const auto config = std::make_shared<const ManagerConfig>(ms, std::move(module_configs));
    EVLOG_info << "Config loading completed in "
               << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - config_start)
                      .count()
               << "ms";

    const auto module_ids = neo::hosted_module_ids(*config, ms);
    EVLOG_info << fmt::format("Hosting {} modules in one process", module_ids.size());

    std::shared_ptr<MQTTAbstraction> external_mqtt;
    if (vm.count("no-bridge") == 0) {
        external_mqtt = connect_bridge(ms.mqtt_settings);
    }

    auto config_service = std::make_shared<config::LocalConfigService>(core);
    core.register_set_runtime_parameter_handler(
        [config_service](const everest::config::ConfigurationParameterIdentifier& cfg_param_id,
                         const std::string& value) {
            const auto result = config_service->set_module_parameter(cfg_param_id, value);
            if (not result.has_value()) {
                return config::SetParameterResponse::SetCallFailed;
            }
            if (result->status == config::SetResponseStatus::Accepted) {
                return config::SetParameterResponse::ModuleReplied_Applied;
            }
            if (result->status == config::SetResponseStatus::RebootRequired) {
                return config::SetParameterResponse::ModuleReplied_RequiresRestart;
            }
            return config::SetParameterResponse::ModuleReplied_Rejected;
        });
    // the handler captures run() locals, the core must not call it after they are gone
    const struct ClearSetParamForwarder {
        config::ConfigServiceCore& core;
        ~ClearSetParamForwarder() {
            core.register_set_runtime_parameter_handler(nullptr);
        }
    } clear_set_param_forwarder{core};

    auto bus = std::make_shared<LocalBus>([config](std::string_view module_id, std::string_view requirement_id) {
        return config->resolve_requirement(module_id, requirement_id);
    });
    for (const auto& module_id : module_ids) {
        bus->register_local_module(module_id, config->printable_identifier(module_id));
    }

    LocalModuleEnvironment environment{bus, external_mqtt, config_service, ms.mqtt_settings};
    const auto startup_start = std::chrono::steady_clock::now();
    auto host = std::make_unique<neo::ModuleHost>(ms, config, core.get_active_module_configurations(), environment);
    host->load_all();
    host->register_all();
    host->init_all();
    host->start_all();
    EVLOG_info << fmt::format(
        TERMINAL_STYLE_OK, "All {} modules are initialized. EVerest up and running in one process [{}ms]",
        module_ids.size(),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - startup_start)
            .count());

    const int signal = wait_for_termination_signal(termination_signals, [&bus] {
        EVLOG_info << "LocalBus state:";
        for (const auto& line : bus->describe_subscriptions()) {
            EVLOG_info << "  " << line;
        }
    });
    EVLOG_info << fmt::format("Received signal {}, shutting down", signal);

    const auto not_stopped = host->shutdown_all(std::chrono::steady_clock::now() + SHUTDOWN_TIMEOUT);
    if (not not_stopped.empty()) {
        EVLOG_warning << fmt::format("Shutdown handlers did not return within {}s: {}", SHUTDOWN_TIMEOUT.count(),
                                     fmt::join(not_stopped, ", "));
    }
    bus->stop();
    EVLOG_info << fmt::format("LocalBus delivered {} typed variable messages in-process", bus->published_count());

    // A module whose ready() runs until the module object is destroyed (EvseV2G and Evse15118D20 poll their own
    // shutdown flag) cannot be torn down. The multi-process manager ends such a module with SIGTERM, without
    // destructors; the equivalent here is to end the process.
    const auto still_running = host->wait_ready_finished(std::chrono::steady_clock::now() + READY_RETURN_TIMEOUT);
    if (not not_stopped.empty() or not still_running.empty()) {
        if (not still_running.empty()) {
            EVLOG_info << fmt::format("ready() still running in {}; ending the process without destructors, as the "
                                      "manager does after SIGTERM",
                                      fmt::join(still_running, ", "));
        }
        EVLOG_info << "everest-neo terminated";
        std::_Exit(EXIT_SUCCESS);
    }
    host.reset();
    if (external_mqtt) {
        external_mqtt->disconnect();
    }
    EVLOG_info << "everest-neo terminated";
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char* argv[]) {
    // Blocked before any thread exists so every worker inherits the mask and only the signalfd receives them.
    sigset_t termination_signals;
    sigemptyset(&termination_signals);
    sigaddset(&termination_signals, SIGINT);
    sigaddset(&termination_signals, SIGTERM);
    sigaddset(&termination_signals, SIGUSR1);
    sigprocmask(SIG_BLOCK, &termination_signals, nullptr);

    po::options_description desc("everest-neo: runs all C++ modules of an EVerest config in one process");
    desc.add_options()("help,h", "produce help message");
    desc.add_options()("version", "Print version and exit");
    desc.add_options()("prefix", po::value<std::string>(), "Prefix path of everest installation");
    desc.add_options()("config", po::value<std::string>(),
                       "Full path to a config file, or the name of a config in the installed config directory");
    desc.add_options()("log_config", po::value<std::string>(), "The path to a custom logging config");
    desc.add_options()("mqtt_broker_host", po::value<std::string>(),
                       fmt::format("External MQTT broker hostname (default {})", defaults::MQTT_BROKER_HOST).c_str());
    desc.add_options()("mqtt_broker_port", po::value<std::uint16_t>(),
                       fmt::format("External MQTT broker port (default {})", defaults::MQTT_BROKER_PORT).c_str());
    desc.add_options()("no-bridge", "Do not connect to an external MQTT broker");
    desc.add_options()("dontvalidateschema", "Don't validate json schema on every message");

    po::variables_map vm;
    try {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);
    } catch (const std::exception& e) {
        std::cerr << "Error during command line parsing: " << e.what() << "\n" << desc << "\n";
        return EXIT_FAILURE;
    }

    if (vm.count("help") != 0) {
        std::cout << desc << "\n";
        return EXIT_SUCCESS;
    }
    if (vm.count("version") != 0) {
        std::cout << argv[0] << " (" << PROJECT_NAME << " " << PROJECT_VERSION << " " << GIT_VERSION << ")\n";
        return EXIT_SUCCESS;
    }

    try {
        return run(vm, termination_signals);
    } catch (boost::exception& e) {
        EVLOG_critical << fmt::format("Caught top level boost::exception:\n{}", boost::diagnostic_information(e, true));
    } catch (std::exception& e) {
        EVLOG_critical << fmt::format("Caught top level std::exception:\n{}", boost::diagnostic_information(e, true));
    }
    return EXIT_FAILURE;
}
