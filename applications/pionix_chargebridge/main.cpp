// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#include "charge_bridge/charge_bridge.hpp"
#include "charge_bridge/status_ui.hpp"
#include "charge_bridge/utilities/string.hpp"
#include <algorithm>
#include <atomic>
#include <charge_bridge/discovery.hpp>
#include <charge_bridge/utilities/logging.hpp>
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
#include <optional>
#include <set>
#include <string>
#include <unistd.h>
#include <vector>

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
        std::cout << "--update_only       use this flag to execute an update and stop the application after\n"
                     "                    no status dashboard is started (--status-output is ignored, no TTY is\n"
                     "                    required), progress goes to stdout and the exit status reports whether\n"
                     "                    every configured instance was updated: a discovery endpoint\n"
                     "                    (ip: ANY_EVSE / ANY_EV) is resolved by a single bounded mDNS discovery\n"
                     "                    first, and an instance that is not reached or not updated fails the run\n";
        std::cout << "--status-output=auto|log|terminal|off\n"
                     "                    output mode for charge_bridge status output.\n"
                     "                    auto: table if stdin and stdout are a TTY, key=value log otherwise\n"
                     "                    log: one-line key=value output\n"
                     "                    terminal: always table output (requires stdin and stdout TTY)\n"
                     "                    off: suppress status output\n";
        std::cout << "--status-refresh-ms=100\n"
                     "                    deprecated: terminal redraws are event-driven, this value is ignored\n";
        std::cout << "--status-message-lines=10\n"
                     "                    terminal message panel height, maximum is 1000\n"
                     "                    shows latest N non-success messages below dashboard\n"
                     "                    the panel never takes more than half of the terminal height, so the\n"
                     "                    dashboard stays visible; the panel scrolls back over 1000 messages\n"
                     "                    0 is clamped to 1: terminal mode captures all diagnostics, so the\n"
                     "                    panel cannot be hidden; use --status-output=log for plain log output\n"
                     "                    ignored in log/off modes\n";
        std::cout << "--status-no-color   disable ANSI colors in the terminal dashboard and in the status\n"
                     "                    and diagnostic output; non-color controls remain unchanged\n"
                     "                    colors are off automatically when stdout is not a TTY\n";
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
std::atomic<int> g_shutdown_signal(0);
void signal_handler(int signum) {
    // Async-signal-safe work only: no stream output from here. Writing to std::cout is not
    // async-signal-safe and would also land in the middle of the terminal dashboard's screen. The
    // signal number is reported by main() once the event loop and the UI are gone.
    g_shutdown_signal.store(signum);
    g_run_application = false;
}

namespace {

// How long --update_only waits for a device to announce itself before it gives up on one instance.
// Matches the per-attempt timeout of the managed path (charge_bridge.cpp), which retries forever -
// an update-only run has to terminate, so there is exactly one attempt.
constexpr auto update_only_discovery_timeout = std::chrono::seconds(10);
constexpr auto update_only_discovery_poll_interval = std::chrono::milliseconds(100);

// mDNS spelling of a config's `ip:` field. Mirrors parse_endpoint_intent()/make_interface_list() in
// charge_bridge.cpp (both are file-local there): "ANY_EVSE"/"ANY_EV" pick the device type, an
// optional suffix restricts the interfaces to search ("ANY_EVSE(eth0,eth1)") or excludes them
// ("ANY_EVSE(!wlan0)"). Only --update_only needs the intent here, because it resolves the endpoint
// itself; every other mode gets it from charge_bridge::manage(). Keep the two in sync.
struct mdns_endpoint {
    discovery_device_type type{discovery_device_type::CB_EVSE};
    std::set<std::string> interfaces;
    bool excluding{false};
};

std::optional<mdns_endpoint> parse_mdns_endpoint(std::string const& cb_remote) {
    mdns_endpoint result;
    std::string pattern;
    if (utilities::string_starts_with(cb_remote, "ANY_EVSE")) {
        result.type = discovery_device_type::CB_EVSE;
        pattern = "ANY_EVSE";
    } else if (utilities::string_starts_with(cb_remote, "ANY_EV")) {
        result.type = discovery_device_type::CB_EV;
        pattern = "ANY_EV";
    } else {
        return std::nullopt;
    }

    // "ANY_EVSE(...)": strip the enclosing parentheses, then an optional leading '!' marks the list
    // as excluding. Anything not carrying a "(...)" list means: search all interfaces.
    auto const raw = utilities::string_after_pattern(cb_remote, pattern);
    if (raw.size() >= 3 && raw.front() == '(' && raw.back() == ')') {
        auto const list = raw.substr(1, raw.size() - 2);
        result.excluding = list.front() == '!';
        result.interfaces = utilities::csv_to_set(list.substr(result.excluding ? 1 : 0));
    }
    return result;
}

// One bounded mDNS discovery attempt, driven by a private event handler: --update_only runs before
// (and without) the application's event loop, so the managed path's discovery - which lives in
// charge_bridge::manage() - is not available. Returns the address of the first matching device, or
// nothing on timeout, abort or a discovery that cannot be started.
std::optional<std::string> discover_update_endpoint(std::string const& cb_name, mdns_endpoint const& endpoint) {
    std::optional<std::string> discovered_ip;
    // Declared before the discovery object so it is destroyed last. Nothing polls it after this
    // function returns, so even an exception escaping the loop below (which skips the unregister) can
    // only leave an entry for an already closed file descriptor in a map that is about to be freed.
    fd_event_handler handler;

    try {
        discovery mdns(endpoint.type, endpoint.interfaces, endpoint.excluding, cb_name);
        mdns.set_discovery_callback([&discovered_ip](everest::lib::io::mdns::mDNS_discovery const& info) {
            if (not discovered_ip.has_value()) {
                // IPv4 preferred, IPv6 (with %scope for link-local) as fallback
                discovered_ip = everest::lib::io::mdns::select_address(info);
            }
        });

        if (not handler.register_event_handler(&mdns)) {
            handler.unregister_event_handler(&mdns);
            utilities::print_error(cb_name, "DISCOVERY", 1) << "Failed to register mDNS discovery handler" << std::endl;
            return std::nullopt;
        }

        utilities::print_info(cb_name, "DISCOVERY") << "Waiting up to " << update_only_discovery_timeout.count()
                                                    << " s for the device to announce itself" << std::endl;

        auto const deadline = std::chrono::steady_clock::now() + update_only_discovery_timeout;
        while (not discovered_ip.has_value() and g_run_application.load() and
               std::chrono::steady_clock::now() < deadline) {
            handler.poll(update_only_discovery_poll_interval);
            handler.run_actions();
        }
        // While mdns is still alive: unregistering calls back into it.
        handler.unregister_event_handler(&mdns);
    } catch (std::exception const& e) {
        utilities::print_error(cb_name, "DISCOVERY", 1) << "mDNS discovery failed: " << e.what() << std::endl;
        return std::nullopt;
    }

    if (not discovered_ip.has_value()) {
        utilities::print_error(cb_name, "DISCOVERY", 1)
            << (g_run_application.load() ? "No ChargeBridge announced itself, nothing to update"
                                         : "Discovery stopped: shutdown requested")
            << std::endl;
        return std::nullopt;
    }

    utilities::print_error(cb_name, "DISCOVERY", 0) << "Discovered at: " << *discovered_ip << std::endl;
    return discovered_ip;
}

// Replaces a discovery endpoint (ip: ANY_EVSE / ANY_EV) by the address of the device that answers.
// Fixed-IP configs are left untouched and always succeed here.
bool resolve_update_endpoint(charge_bridge_config& config) {
    auto const endpoint = parse_mdns_endpoint(config.cb_remote);
    if (not endpoint.has_value()) {
        return true;
    }

    auto const discovered_ip = discover_update_endpoint(config.cb_name, *endpoint);
    if (not discovered_ip.has_value()) {
        return false;
    }

    // update_firmware() only ever uses the firmware endpoint; cb_remote is rewritten as well so both
    // stay consistent. The per-bridge endpoints are deliberately left alone: no bridge is created in
    // update-only mode (charge_bridge::manage() is never called).
    config.cb_remote = *discovered_ip;
    config.firmware.cb_remote = *discovered_ip;
    return true;
}

// --update_only: flash every configured instance and stop, without an event loop, manager threads or
// a status dashboard. Returns the process exit code; EXIT_SUCCESS means every configured instance was
// updated successfully.
int run_update_only(std::vector<charge_bridge_config> const& cb_configs) {
    auto abort_requested = []() { return not g_run_application.load(); };
    auto all_ok = true;

    for (std::size_t idx = 0; idx < cb_configs.size(); ++idx) {
        // A copy: resolving a discovery endpoint rewrites it.
        auto config = cb_configs[idx];
        print_charge_bridge_config(config);

        if (not resolve_update_endpoint(config)) {
            all_ok = false;
        } else {
            ::charge_bridge::charge_bridge cb(config);
            // Signal handlers are installed already, so let Ctrl-C interrupt the upload here too. No
            // status UI is running, so the upload's progress goes to plain stdout (the print sink is
            // installed by ui.run(), see status_ui::run()).
            all_ok = cb.update_firmware(true, abort_requested) and all_ok;
        }

        if (abort_requested()) {
            // A signal that arrives while - or after - an instance is updated ends the run here. The
            // instance's own result stands as update_firmware() reported it: an upload that was cut
            // short fails on its own, a completed one stays a success (a signal must not turn it into
            // a failure). Only instances that are skipped from here on make the run fail.
            auto const skipped = cb_configs.size() - idx - 1;
            if (skipped > 0) {
                utilities::print_error("", "FIRMWARE", 1)
                    << "Firmware update stopped: " << skipped << " configured instance(s) not updated" << std::endl;
                all_ok = false;
            }
            break;
        }
    }

    if (auto const signum = g_shutdown_signal.load(); signum != 0) {
        std::cout << "\nSignal " << signum << " received. Firmware update stopped." << std::endl;
    }
    return all_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

} // namespace

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
    // --update_only neither starts the dashboard nor reads stdin (see run_update_only), so none of the
    // status output setup below applies to it - in particular not the TTY requirement, which used to
    // refuse an update whose output was redirected.
    auto const update_only = mode_of_operation == mode::update_only;

    // The terminal dashboard also reads and raw-modes stdin, so both fds must be TTYs. A TTY stdout with
    // an unusable stdin (redirected from /dev/null, closed fd 0) makes the input listener spin at 100% CPU.
    bool stdin_is_tty = isatty(STDIN_FILENO);
    bool stdout_is_tty = isatty(STDOUT_FILENO);
    bool terminal_usable = stdin_is_tty && stdout_is_tty;
    if (not update_only && status_output_mode == utilities::status_output_mode::terminal && not terminal_usable) {
        if (not stdin_is_tty) {
            std::cerr << "--status-output=terminal requires stdin to be a TTY" << std::endl;
        }
        if (not stdout_is_tty) {
            std::cerr << "--status-output=terminal requires stdout to be a TTY" << std::endl;
        }
        return EXIT_FAILURE;
    }

    // One decision for every diagnostic written to stdout: the key=value status lines, print_error and
    // print_info (the dashboard has its own no_color handling). Escapes are pointless in a redirected
    // log and --status-no-color drops them on a TTY as well.
    utilities::set_diagnostic_color_enabled(stdout_is_tty and not ui_options.no_color);

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

    if (update_only) {
        // Done before any UI exists: --update_only stops the application after the update, as
        // documented in --help - no bridges are managed and no dashboard is started.
        return run_update_only(cb_configs);
    }

    auto effective_status_output_mode = status_output_mode;
    if (status_output_mode == utilities::status_output_mode::auto_mode) {
        effective_status_output_mode =
            terminal_usable ? utilities::status_output_mode::terminal : utilities::status_output_mode::log;
    }
    status_ui_options effective_ui_options;
    effective_ui_options.status_output = effective_status_output_mode;
    effective_ui_options.status_refresh_ms = ui_options.status_refresh_ms;
    effective_ui_options.status_message_lines = ui_options.status_message_lines;
    effective_ui_options.no_color = ui_options.no_color;
    // In terminal mode the UI captures every print_error/print_info line, so a hidden message panel
    // would drop all diagnostics. status_ui clamps the panel to one line; tell the user about it.
    if (effective_status_output_mode == utilities::status_output_mode::terminal &&
        effective_ui_options.status_message_lines == 0) {
        std::cerr << "--status-message-lines=0 would hide all diagnostics in terminal mode; using 1 line. "
                     "Use --status-output=log for plain log output without a dashboard."
                  << std::endl;
    }

    status_ui ui(effective_ui_options, cb_names);
    // Quitting the terminal UI (q / Ctrl-C) must shut down the whole application. ftxui installs its
    // own SIGINT handler while its loop runs, so we cannot rely on the signal reaching the app; the
    // UI thread notifies us here. add_action wakes the (possibly blocking) event loop poll.
    ui.set_quit_handler([&]() {
        g_run_application = false;
        ev_handler.add_action([]() {});
    });
    auto status_sink = [&ui](utilities::chargebridge_status status) { ui.publish(std::move(status)); };
    // The terminal UI wants a live (every-tick) feed for its readouts and telemetry charts; log mode
    // keeps emitting only on status changes, so its tick sink stays empty.
    std::function<void(utilities::chargebridge_status)> tick_sink;
    if (effective_status_output_mode == utilities::status_output_mode::terminal) {
        tick_sink = [&ui](utilities::chargebridge_status status) { ui.publish(std::move(status)); };
    }
    std::vector<std::unique_ptr<::charge_bridge::charge_bridge>> cb_handler;

    for (auto const& config : cb_configs) {
        print_charge_bridge_config(config);
        cb_handler.push_back(std::make_unique<::charge_bridge::charge_bridge>(config, status_sink, tick_sink));
    }

    ui.run();

    for (auto& cb : cb_handler) {
        auto force_update = mode_of_operation == mode::update;
        cb->manage(ev_handler, g_run_application, force_update);
    }

    ev_handler.run(g_run_application);
    // Stop the UI first: it owns the terminal (ftxui alternate screen), and destroying the bridges
    // joins their manager threads, which can still be finishing a cancelled firmware upload. Doing it
    // the other way round leaves the user staring at a frozen dashboard with no output.
    //
    // Safe in this order because status_ui::stop() only lowers flags, drains its queue and joins the
    // loop thread — the ftxui screen is created and destroyed exclusively on that thread. Afterwards
    // status_ui::publish() from the bridges is still valid (terminal mode: mutex-guarded state plus a
    // redraw flag; log mode: a push into the stopped queue, which is a no-op), and print_error falls
    // back to stdout once the print sink is cleared — by then the terminal is restored, so plain
    // stdout is exactly where those late diagnostics belong. `ui` outlives cb_handler in both this
    // sequence and the reverse-order destruction at the end of main.
    ui.stop();
    cb_handler.clear();
    // Reported here, not from the signal handler (see signal_handler): at this point the terminal UI
    // has restored the screen, so plain stdout is safe again.
    if (auto const signum = g_shutdown_signal.load(); signum != 0) {
        std::cout << "\nSignal " << signum << " received. Graceful shutdown completed." << std::endl;
    }
    return EXIT_SUCCESS;
}
