// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <atomic>
#include <csignal>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

#include <iso15118/io/logging.hpp>
#include <iso15118/tbd_controller.hpp>

namespace dt = iso15118::message_20::datatypes;

namespace {
std::atomic<bool> running{true};
std::mutex controller_mutex;

void signal_handler(int signal) {
    if (signal == SIGINT) {
        std::cout << "SIGINT\n";
        running = false;
    }
}
} // namespace

namespace mock {

// Placeholder functions
bool check_dlink_ready() {
    return false;
}

std::optional<std::vector<dt::ServiceCategory>> check_energy_modes() {
    return std::nullopt;
}

} // namespace mock

int main() {
    using namespace iso15118;
    using namespace std::chrono_literals;

    std::unique_ptr<TbdController> controller;

    session::logging::set_session_log_callback([](std::size_t, const session::logging::Event& event) {
        if (const auto* simple_event = std::get_if<session::logging::SimpleEvent>(&event)) {
            std::cout << "log(session: simple event): " << simple_event->info << "\n";
        } else {
            std::cout << "log(session): not decoded\n";
        }
    });

    io::set_logging_callback([](LogLevel level, const std::string& message) {
        std::cout << "log(" << static_cast<int>(level) << "): " << message << "\n";
    });

    // Setup signal handler for graceful shutdown
    std::signal(SIGINT, signal_handler);

    // For a tcp only and disabled sdp server setup this is enough.
    // SSLConfig does not need any initialisation.
    TbdConfig config{};
    config.interface_name = "wlp0s20f3";
    config.tls_negotiation_strategy = config::TlsNegotiationStrategy::ENFORCE_NO_TLS;
    config.enable_sdp_server = false;

    // Very minimal callback setup as an example
    session::feedback::Callbacks callbacks{};
    callbacks.signal = [](session::feedback::Signal signal) {
        std::cout << "Signal callback: " << static_cast<uint32_t>(signal) << "\n";
    };
    callbacks.v2g_message = [](iso15118::message_20::Type id) {
        std::cout << "V2G Message ID callback: " << static_cast<uint32_t>(id) << "\n";
    };
    callbacks.evccid = [](const std::string& evccid) { std::cout << "EVCCID callback: " << evccid << "\n"; };
    callbacks.selected_protocol = [](const std::string& protocol) {
        std::cout << "Selected protocol callback: " << protocol << "\n";
    };

    // Creating EvseSetupConfig for a simple d20 ac + dynamic charger without bpt and pnc
    d20::EvseSetupConfig evse_config{};
    evse_config.evse_id = "DE*PNX*E12345*1";
    evse_config.supported_energy_services.emplace_back(dt::ServiceCategory::AC);
    evse_config.authorization_services.emplace_back(dt::Authorization::EIM);
    evse_config.enable_certificate_install_service = false;

    d20::AcTransferLimits ac_limits{};
    ac_limits.charge_power.max = dt::from_float(11000);
    ac_limits.charge_power.min = dt::from_float(1300);
    ac_limits.nominal_frequency = dt::from_float(50);
    evse_config.ac_limits = ac_limits;

    // Only dynamic mode is supported
    evse_config.control_mobility_modes.emplace_back<d20::ControlMobilityNeedsModes>(
        {dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedByEvcc});

    evse_config.ac_setup_config.emplace<d20::AcSetupConfig>(
        {230, {dt::AcConnector::SinglePhase, dt::AcConnector::ThreePhase}});

    controller = std::make_unique<TbdController>(std::move(config), std::move(callbacks), std::move(evse_config));

    // Start loop in a separate thread
    std::thread controller_thread([&controller]() { controller->loop(); });

    // Very simple example how to update variables with the controller.
    // In a production setup an async framework like libuv should be used
    while (running) {
        if (mock::check_dlink_ready()) {
            std::lock_guard<std::mutex> lock(controller_mutex);
            controller->set_dlink_ready(true);
        }
        const auto updated_energy_modes = mock::check_energy_modes();
        if (updated_energy_modes.has_value()) {
            std::lock_guard<std::mutex> lock(controller_mutex);
            controller->update_energy_modes(updated_energy_modes.value());
        }

        std::this_thread::sleep_for(100ms);
    }

    controller->shutdown();

    // Join the thread to ensure proper cleanup
    if (controller_thread.joinable()) {
        controller_thread.join();
    }

    return 0;
}
