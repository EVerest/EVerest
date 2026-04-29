// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "command_handlers.hpp"

#include <iomanip>
#include <iostream>
#include <thread>

namespace everest::lifecycle_cli {

constexpr int MONITOR_TABLE_WIDTH = 120;

CommandHandlers::CommandHandlers(std::shared_ptr<LifecycleServiceClientIfc> client) : m_client(client) {
}

void CommandHandlers::stop_modules() {
    std::cout << "Sending stop_modules request...\n";
    auto result = m_client->stop_modules();

    if (!result) {
        std::cerr << "Failed to stop modules\n";
        return;
    }

    std::cout << "Stop modules result:\n";
    std::cout << "  Status: " << result->status << "\n";
}

void CommandHandlers::start_modules() {
    std::cout << "Sending start_modules request...\n";
    auto result = m_client->start_modules();

    if (!result) {
        std::cerr << "Failed to start modules\n";
        return;
    }

    std::cout << "Start modules result:\n";
    std::cout << "  Status: " << result->status << "\n";
}

void CommandHandlers::get_everest_version() {
    std::cout << "Sending get_everest_version request...\n";
    auto result = m_client->get_everest_version();

    if (!result) {
        std::cerr << "Failed to get EVerest version\n";
        return;
    }

    std::cout << "Get EVerest version result:\n";
    std::cout << "  Name: " << result->name << "\n";
    std::cout << "  Based on Release: " << result->version << "\n";
    std::cout << "  GIT-Version: " << result->git_version << "\n";
}

void CommandHandlers::monitor() {
    std::cout << "Monitoring lifecycle status. Press Ctrl+C to stop.\n";
    std::cout << std::string(MONITOR_TABLE_WIDTH, '-') << "\n";
    std::cout << std::left << std::setw(30) << "Timestamp" << std::setw(20) << "Module Status" << std::setw(20)
              << "EVerest Running" << std::setw(30) << "Config API" << std::setw(20) << "Lifecycle API"
              << "\n";
    std::cout << std::string(MONITOR_TABLE_WIDTH, '-') << "\n";

    m_client->subscribe_to_status_updates([](const API_types_lc::ExecutionStatusUpdateNotice& status) {
        std::cout << std::left << std::setw(30) << status.tstamp.value_or("<no-tstamp>") << std::setw(20)
                  << (status.module_status.has_value() ? API_types_lc::serialize(status.module_status.value())
                                                       : std::string())
                  << std::setw(20) << (status.everest_running ? "true" : "false") << std::setw(30)
                  << (status.configuration_api_available.has_value()
                          ? API_types_lc::serialize(status.configuration_api_available.value())
                          : std::string())
                  << std::setw(20)
                  << (status.lifecycle_api_ro.has_value() ? (status.lifecycle_api_ro.value() ? "RO" : "RW")
                                                          : std::string())
                  << "\n";
    });

    // Keep the program running to receive updates
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

} // namespace everest::lifecycle_cli
