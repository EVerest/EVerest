// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <EEBUS.hpp>

#include <cstddef>
#include <filesystem>
#include <limits>
#include <string>

#include <everest/logging.hpp>
#include <everest/run_application/run_application.hpp>

#include <ConfigValidator.hpp>
#include <EebusConnectionHandler.hpp>

namespace {

/// \brief Parse the log level from an eebus-grpc output line and log it at the appropriate EVerest level.
///        Expected format: "<datetime> <LEVEL> <message>" e.g. "2026-03-18 22:20:42 INFO Certificate path: ..."
void log_grpc_output(const std::string& line) {
    // The level token follows the second space (after "YYYY-MM-DD HH:MM:SS").
    // Find the position after "YYYY-MM-DD " (11 chars) then after "HH:MM:SS " (9 chars) = 20 chars minimum.
    static constexpr std::size_t datetime_prefix_len = 20; // "YYYY-MM-DD HH:MM:SS "
    if (line.size() > datetime_prefix_len) {
        auto level_end = line.find(' ', datetime_prefix_len);
        if (level_end != std::string::npos) {
            auto level = line.substr(datetime_prefix_len, level_end - datetime_prefix_len);
            auto message = line.substr(level_end + 1);
            if (level == "DEBUG" || level == "TRACE") {
                EVLOG_debug << message;
                return;
            }
            if (level == "INFO") {
                EVLOG_info << message;
                return;
            }
            if (level == "WARNING" || level == "WARN") {
                EVLOG_warning << message;
                return;
            }
            if (level == "ERROR" || level == "FATAL") {
                EVLOG_error << message;
                return;
            }
        }
    }
    // Fallback: log the full line as info if we can't parse the level
    EVLOG_info << "eebus-grpc: " << line;
}

} // namespace

namespace module {

EEBUS::~EEBUS() {
    // 1. Signal all threads to stop
    m_running_flag = false;
    m_eebus_grpc_api_thread_active.store(false);
    m_eebus_grpc_api_stop_requested->store(true);

    // 2. Stop connection handler first — sets its internal stop flag to break out of
    //    any in-progress wait_for_channel_ready() call, and cancels the gRPC stream.
    if (m_connection_handler) {
        m_connection_handler->stop();
    }

    // 3. Wake the event handler's epoll so it checks m_running_flag and exits.
    //    fd_event_handler::run() uses epoll_wait(-1) (infinite timeout); setting
    //    m_running_flag alone does NOT wake it. add_action() writes to an internal
    //    eventfd which wakes epoll_wait.
    m_event_handler.add_action([] {});

    // 4. Wake the grpc api thread if it's sleeping between restart attempts.
    m_shutdown_cv.notify_all();

    // 5. Join threads
    if (m_event_handler_thread.joinable()) {
        m_event_handler_thread.join();
    }
    if (m_eebus_grpc_api_thread.joinable()) {
        m_eebus_grpc_api_thread.join();
    }
}

void EEBUS::init() {
    // Setup callbacks
    m_callbacks.update_limits_callback = [this](types::energy::ExternalLimits new_limits) {
        r_eebus_energy_sink->call_set_external_limits(std::move(new_limits));
    };

    auto config_validator = std::make_shared<ConfigValidator>(config, info.paths.etc, info.paths.libexec);
    if (!config_validator->validate()) {
        EVLOG_critical << "EEBUS module configuration is invalid";
        return;
    }

    if (config.manage_eebus_grpc_api_binary) {
        m_eebus_grpc_api_thread_active.store(true);
        m_eebus_grpc_api_thread =
            std::thread(&EEBUS::start_eebus_grpc_api, this, config_validator->get_eebus_grpc_api_binary_path(),
                        config_validator->get_grpc_port(), config_validator->get_certificate_path(),
                        config_validator->get_private_key_path(), config_validator->get_restart_delay_s());
    }

    m_connection_handler = std::make_unique<EebusConnectionHandler>(config_validator);
    m_event_handler.register_event_handler(m_connection_handler.get());

    if (!m_connection_handler->add_use_case(EebusUseCase::LPC, m_callbacks)) {
        EVLOG_critical << "Failed to add LPC use case; EEBUS module will not function.";
        return;
    }
    m_connection_handler->done_adding_use_case();
}

void EEBUS::start_eebus_grpc_api(const std::filesystem::path& binary_path, int port,
                                 const std::filesystem::path& cert_file, const std::filesystem::path& key_file,
                                 int restart_delay_s) {
    std::vector<std::string> args;
    constexpr int num_args = 6;
    args.reserve(num_args);
    args.emplace_back("-port");
    args.emplace_back(std::to_string(port));
    args.emplace_back("-certificate-path");
    args.emplace_back(cert_file.string());
    args.emplace_back("-private-key-path");
    args.emplace_back(key_file.string());

    while (m_eebus_grpc_api_thread_active) {
        try {
            EVLOG_info << "Starting eebus_grpc_api binary...";
            everest::run_application::RunOptions opts;
            opts.callback = [this](const std::string& output_line) {
                if (!output_line.empty()) {
                    log_grpc_output(output_line);
                }
                if (not m_eebus_grpc_api_thread_active) {
                    return everest::run_application::CmdControl::Terminate;
                }
                return everest::run_application::CmdControl::Continue;
            };
            // Retain only the first and last 50 lines (100 total), dropping the middle, so a
            // long-lived child cannot grow this buffer without bound. Cap by lines, not bytes.
            constexpr std::size_t head_lines = 50;
            constexpr std::size_t tail_lines = 50;
            constexpr std::size_t unbounded_bytes = std::numeric_limits<std::size_t>::max();
            opts.accumulation =
                everest::run_application::RetainHeadTail{head_lines, unbounded_bytes, tail_lines, unbounded_bytes};
            opts.stop_requested = m_eebus_grpc_api_stop_requested;
            opts.kill_child_on_parent_death = true;

            everest::run_application::CmdOutput output =
                everest::run_application::run_application(binary_path.string(), args, opts);
            if (m_eebus_grpc_api_thread_active) {
                EVLOG_warning << "eebus-grpc process exited with code: " << output.exit_code;
            } else {
                EVLOG_info << "eebus-grpc process terminated on shutdown";
            }
        } catch (const std::exception& e) {
            EVLOG_critical << "start_eebus_grpc_api thread caught exception: " << e.what();
        } catch (...) {
            EVLOG_critical << "start_eebus_grpc_api thread caught unknown exception.";
        }

        if (m_eebus_grpc_api_thread_active) {
            EVLOG_info << "Restarting eebus_grpc_api binary in " << restart_delay_s << " seconds...";
            std::unique_lock<std::mutex> lock(m_shutdown_mutex);
            m_shutdown_cv.wait_for(lock, std::chrono::seconds(restart_delay_s),
                                   [this] { return !m_eebus_grpc_api_thread_active.load(); });
        }
    }
    EVLOG_info << "eebus_grpc_api monitoring thread is stopping.";
}

void EEBUS::ready() {
    if (!m_connection_handler) {
        EVLOG_critical << "EEBUS module failed to initialize; not starting event handler thread.";
        return;
    }

    // Start the event handler in its own thread
    m_event_handler_thread = std::thread([this]() { m_event_handler.run(m_running_flag); });
}

} // namespace module
