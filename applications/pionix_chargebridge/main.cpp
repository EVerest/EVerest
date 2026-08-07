// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#include "charge_bridge/charge_bridge.hpp"
#include "charge_bridge/status_ui.hpp"
#include "charge_bridge/utilities/string.hpp"
#include <algorithm>
#include <atomic>
#include <charge_bridge/utilities/parse_config.hpp>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/timer_fd.hpp>
#include <iostream>
#include <limits>
#include <numeric>
#include <unistd.h>

using namespace everest::lib::io::event;
using namespace everest::lib::API::V1_0::types;
using namespace charge_bridge;

enum class mode {
    error,
    connector,
    update,
    update_only,
};

mode parse_args(int argc, char* argv[], std::vector<std::string>& config_files,
                utilities::status_output_mode& status_output_mode, status_ui_options& status_ui_opts) {
    // clang-format off
    auto print_msg = []() {
        std::cout << "\nUSAGE: \n";
        std::cout << "pionix_chargebridge [--update][--update_only][--status-output=auto|log|terminal|off] "
                     "[--status-refresh-ms=100] [--status-message-lines=10] [--status-no-color] {config_file [config_file_2 ....]} \n";
        std::cout << "\n";
        std::cout << "--update            use this flag to execute an update at start and continue operation after\n";
        std::cout << "--update_only       use this flag to execute an update and stop the application after\n";
        std::cout << "--status-output=auto|log|terminal|off\n"
                     "                    output mode for charge_bridge status output.\n"
                     "                    auto: table in TTY, key=value log otherwise\n"
                     "                    log: one-line key=value output\n"
                     "                    terminal: always table output (requires stdout TTY)\n"
                     "                    off: suppress status output\n";
        std::cout << "--status-refresh-ms=100\n"
                     "                    terminal refresh rate in milliseconds, minimum is 0 (immediate)\n"
                     "                    ignored in log/off modes\n";
        std::cout << "--status-message-lines=10\n"
                     "                    terminal message buffer size, minimum is 0 (disabled), maximum is 1000\n"
                     "                    shows latest N non-success messages below dashboard\n"
                     "                    ignored in log/off modes\n";
        std::cout << "--status-no-color  disable ANSI colors in the terminal dashboard output\n"
                     "                    message area and non-color controls remain unchanged\n";
        std::cout << "config_file         use this configuration file\n";
        std::cout << "config_file_x       add more configuration files for each additional ChargeBridge group\n";
        std::cout << "\n";
    };
    // clang-format on

    status_output_mode = utilities::status_output_mode::auto_mode;
    status_ui_opts.status_refresh_ms = std::chrono::milliseconds(100);
    auto mode = mode::connector;
    for (int i = 1; i < argc; ++i) {
        std::string current_arg = argv[i];
        if (current_arg == "--update_only") {
            mode = mode::update_only;
        } else if (current_arg == "--update") {
            mode = mode::update;
        } else if (utilities::string_starts_with(current_arg, "--status-output=")) {
            auto output = current_arg.substr(std::string("--status-output=").size());
            if (output == "auto") {
                status_output_mode = utilities::status_output_mode::auto_mode;
            } else if (output == "log") {
                status_output_mode = utilities::status_output_mode::log;
            } else if (output == "terminal") {
                status_output_mode = utilities::status_output_mode::terminal;
            } else if (output == "off") {
                status_output_mode = utilities::status_output_mode::off;
            } else {
                mode = mode::error;
                break;
            }
        } else if (utilities::string_starts_with(current_arg, "--status-refresh-ms=")) {
            auto value = current_arg.substr(std::string("--status-refresh-ms=").size());
            if (value.empty()) {
                mode = mode::error;
                break;
            }
            try {
                if (value.find_first_not_of("0123456789") != std::string::npos) {
                    mode = mode::error;
                    break;
                }
                auto refresh_ms = std::stoull(value);
                using status_refresh_rep = std::chrono::milliseconds::rep;
                constexpr auto max_refresh_ms =
                    std::min<std::uint64_t>(static_cast<std::uint64_t>(std::numeric_limits<status_refresh_rep>::max()),
                                            static_cast<std::uint64_t>(std::numeric_limits<int>::max()));
                if (refresh_ms > max_refresh_ms) {
                    mode = mode::error;
                    break;
                }
                status_ui_opts.status_refresh_ms =
                    std::chrono::milliseconds(static_cast<status_refresh_rep>(refresh_ms));
            } catch (...) {
                mode = mode::error;
                break;
            }
        } else if (utilities::string_starts_with(current_arg, "--status-message-lines=")) {
            auto value = current_arg.substr(std::string("--status-message-lines=").size());
            if (value.empty()) {
                mode = mode::error;
                break;
            }

            if (value.find_first_not_of("0123456789") != std::string::npos) {
                mode = mode::error;
                break;
            }

            try {
                auto lines = std::stoull(value);
                constexpr std::size_t max_status_message_lines = 1000;
                if (lines > max_status_message_lines) {
                    mode = mode::error;
                    break;
                }
                status_ui_opts.status_message_lines = static_cast<std::size_t>(lines);
            } catch (...) {
                mode = mode::error;
                break;
            }
        } else if (current_arg == "--status-no-color") {
            status_ui_opts.no_color = true;
        } else if (utilities::string_starts_with(current_arg, "--")) {
            mode = mode::error;
            break;
        } else {
            config_files.push_back(current_arg);
        }
    }

    if (config_files.size() == 0) {
        mode = mode::error;
    }

    if (mode == mode::error) {
        print_msg();
    }
    return mode;
}

std::atomic<bool> g_run_application(true);
void signal_handler(int signum) {
    std::cout << "\nSignal " << signum << " received. Initiating graceful shutdown." << std::endl;
    g_run_application = false;
}

int main(int argc, char* argv[]) {
    std::cout << "PIONIX ChargeBridge (C) 2025-2026\n" << std::endl;

    std::signal(SIGINT, signal_handler);
    std::signal(SIGHUP, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::vector<std::string> config_files;
    utilities::status_output_mode status_output_mode;
    status_ui_options ui_options;
    fd_event_handler ev_handler;
    std::vector<charge_bridge_config> cb_configs;
    std::vector<std::string> cb_names;
    std::set<std::string> cb_ids_in_use;

    auto mode_of_operation = parse_args(argc, argv, config_files, status_output_mode, ui_options);
    if (mode_of_operation == mode::error) {
        return EXIT_FAILURE;
    }
    bool stdout_is_tty = isatty(STDOUT_FILENO);
    if (status_output_mode == utilities::status_output_mode::terminal && not stdout_is_tty) {
        std::cerr << "--status-output=terminal requires stdout to be a TTY" << std::endl;
        return EXIT_FAILURE;
    }

    auto effective_status_output_mode = status_output_mode;
    if (status_output_mode == utilities::status_output_mode::auto_mode) {
        effective_status_output_mode =
            stdout_is_tty ? utilities::status_output_mode::terminal : utilities::status_output_mode::log;
    }
    status_ui_options effective_ui_options;
    effective_ui_options.status_output = effective_status_output_mode;
    effective_ui_options.status_refresh_ms = ui_options.status_refresh_ms;
    effective_ui_options.status_message_lines = ui_options.status_message_lines;
    effective_ui_options.no_color = ui_options.no_color;
    for (auto const& elem : config_files) {
        auto config_list = utilities::parse_config_multi(elem);
        if (config_list.empty()) {
            return EXIT_FAILURE;
        }

        for (auto const& config : config_list) {
            if (cb_ids_in_use.count(config.cb_name) > 0) {
                std::cerr << "Duplicate charge_bridge::name '" << config.cb_name << "'" << std::endl;
                return EXIT_FAILURE;
            }

            cb_ids_in_use.insert(config.cb_name);
            cb_names.push_back(config.cb_name);
            cb_configs.push_back(config);
        }
    }

    status_ui ui(effective_ui_options, cb_names);
    auto status_sink = [&ui](utilities::chargebridge_status status) { ui.publish(std::move(status)); };
    std::vector<std::unique_ptr<::charge_bridge::charge_bridge>> cb_handler;

    for (auto const& config : cb_configs) {
        print_charge_bridge_config(config);
        cb_handler.push_back(std::make_unique<::charge_bridge::charge_bridge>(config, status_sink));
        auto& cb = *cb_handler.rbegin();

        if (mode_of_operation == mode::update_only) {
            cb->update_firmware(true);
        }
    }

    ui.run();

    for (auto& cb : cb_handler) {
        auto force_update = mode_of_operation == mode::update;
        cb->manage(ev_handler, g_run_application, force_update);
    }

    ev_handler.run(g_run_application);
    cb_handler.clear();
    ui.stop();
    return EXIT_SUCCESS;
}
