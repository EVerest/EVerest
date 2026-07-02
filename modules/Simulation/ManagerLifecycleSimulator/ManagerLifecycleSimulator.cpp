// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "ManagerLifecycleSimulator.hpp"

#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

namespace module {

namespace {
constexpr auto BLOCKED_SHUTDOWN_SLEEP_DURATION = std::chrono::milliseconds(100000);
} // namespace

void ManagerLifecycleSimulator::init() {
    const std::string EXIT_TOPIC = "everest_api/" + this->info.id + "/cmd/exit";
    EVLOG_info << "ManagerLifecycleSimulator: subscribing to exit command on topic: " << EXIT_TOPIC;
    this->mqtt.subscribe(EXIT_TOPIC, [this](const std::string& payload) {
        int exit_code = EXIT_SUCCESS;
        if (!payload.empty()) {
            try {
                std::size_t parsed_chars = 0;
                exit_code = std::stoi(payload, &parsed_chars);
                if (parsed_chars != payload.size()) {
                    EVLOG_warning << "ManagerLifecycleSimulator: exit payload contains trailing characters (payload='"
                                  << payload << "'). Using parsed exit code " << exit_code << ".";
                }
            } catch (const std::exception& e) {
                EVLOG_warning << "ManagerLifecycleSimulator: invalid exit payload '" << payload
                              << "'. Expected integer, defaulting to exit code 0. Error: " << e.what();
                exit_code = EXIT_SUCCESS;
            }
        }

        EVLOG_info << "ManagerLifecycleSimulator: exit command received via MQTT, exiting process with code "
                   << exit_code << ".";
        std::exit(exit_code);
    });

    const std::string BLOCK_TOPIC = "everest_api/" + this->info.id + "/cmd/block";
    EVLOG_info << "ManagerLifecycleSimulator: subscribing to block command on topic: " << BLOCK_TOPIC;
    this->mqtt.subscribe(BLOCK_TOPIC, [this](const std::string& /*payload*/) {
        EVLOG_info << "ManagerLifecycleSimulator: blocking command received via MQTT, blocking process.";
        this->blocked = true;
    });
}

void ManagerLifecycleSimulator::ready() {
}

void ManagerLifecycleSimulator::shutdown() {
    if (this->blocked) {
        EVLOG_info << "ManagerLifecycleSimulator: shutdown command received via framework, but process is blocked.";
        std::this_thread::sleep_for(BLOCKED_SHUTDOWN_SLEEP_DURATION);
    }
    EVLOG_info << "ManagerLifecycleSimulator: shutdown command received via framework, exiting process.";
    std::exit(EXIT_SUCCESS);
}

} // namespace module
