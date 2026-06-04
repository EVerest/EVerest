// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "shm_ping_pong_common.hpp"

#include <chrono>
#include <iostream>

#include <everest/io/event/fd_event_handler.hpp>

using namespace everest::lib::io;
using namespace everest::lib::io::examples::shm_ping_pong;

int main(int argc, char* argv[]) {
    runtime_options runtime;
    if (!parse_args(argc, argv, runtime)) {
        return argc > 1 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") ? 0 : 1;
    }

    install_signal_handlers();

    auto options = make_server_options(runtime);
    std::cout << "SHM ping-pong server\n";
    std::cout << "  shm: " << options.shm_name << "\n";
    std::cout << "  control socket: " << options.control_socket_name << "\n";
    std::cout << "  topics:\n";
    for (const auto& topic : options.topics) {
        std::cout << "    " << topic.name << " (" << topic.total_slots << " slots, " << topic.slot_size << " bytes)\n";
    }
    std::cout << "  press Ctrl-C to stop\n" << std::flush;

    shm::shm_server server(std::move(options));
    server.set_error_handler([](shm::io_status status, std::string_view message) {
        std::cerr << "ERROR: server: " << shm::to_string(status) << ": " << message << "\n";
    });

    const auto open_result = server.open();
    if (uds_bind_unavailable(open_result)) {
        std::cerr << "SKIP: UDS bind is not permitted in this environment\n";
        return 0;
    }
    if (log_io_error(open_result, "server open")) {
        return 1;
    }

    event::fd_event_handler handler;
    if (!server.register_events(handler)) {
        std::cerr << "ERROR: failed to register server events\n";
        (void)server.close();
        return 1;
    }

    while (g_running != 0) {
        handler.poll(std::chrono::milliseconds(1000));
        handler.run_actions();
    }

    bool cleanup_ok = server.unregister_events(handler);
    cleanup_ok = !log_io_error(server.close(), "server close") && cleanup_ok;
    if (!cleanup_ok) {
        return 1;
    }

    std::cout << "SHM ping-pong server stopped\n";
    return 0;
}
