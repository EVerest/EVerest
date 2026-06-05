// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "shm_ping_pong_common.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/timer_fd.hpp>

using namespace everest::lib::io;
using namespace everest::lib::io::examples::shm_ping_pong;
using namespace std::chrono_literals;

int main(int argc, char* argv[]) {
    runtime_options runtime;
    if (!parse_args(argc, argv, runtime)) {
        return argc > 1 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") ? 0 : 1;
    }

    install_signal_handlers();

    shm::shm_client client(make_client_options(runtime, "client_a"));
    client.set_error_handler([](shm::io_status status, std::string_view message) {
        std::cerr << "ERROR: client_a: " << shm::to_string(status) << ": " << message << "\n";
    });

    std::cout << "SHM ping-pong client A\n";
    std::cout << "  control socket: " << runtime.control_socket << "\n";
    std::cout << "  publishes: " << ping_topic << " every 500 ms\n";
    std::cout << "  subscribes: " << pong_topic << "\n";
    std::cout << "  press Ctrl-C to stop\n" << std::flush;

    if (log_io_error(client.connect(), "client_a connect")) {
        return 1;
    }

    const auto subscribe_result = client.subscribe(pong_topic, [](std::string_view topic, std::string_view payload) {
        std::cout << "client_a received " << topic << ": " << payload << "\n";
    });
    if (log_io_error(subscribe_result, "client_a subscribe")) {
        (void)client.disconnect();
        return 1;
    }

    event::fd_event_handler handler;
    if (!client.register_events(handler)) {
        std::cerr << "ERROR: failed to register client_a SHM events\n";
        (void)client.disconnect();
        return 1;
    }

    event::timer_fd timer;
    if (!timer.set_timeout(500ms)) {
        std::cerr << "ERROR: failed to start 500 ms ping timer\n";
        (void)client.unregister_events(handler);
        (void)client.disconnect();
        return 1;
    }

    std::uint64_t seq = 0;
    const auto publish_ping = [&client, &seq]() {
        const auto payload = make_ping_payload(++seq);
        shm::publish_options options;
        options.full_buffer_behavior = shm::publish_full_buffer_behavior::fail;
        const auto result = client.publish(ping_topic, payload, options);
        if (!result) {
            log_io_error(result, "client_a publish ping");
            return;
        }
        std::cout << "client_a published " << ping_topic << ": " << payload << "\n";
    };

    if (!handler.register_event_handler(&timer, publish_ping)) {
        std::cerr << "ERROR: failed to register client_a timer\n";
        (void)client.unregister_events(handler);
        (void)client.disconnect();
        return 1;
    }

    while (g_running != 0 && client.is_connected()) {
        handler.poll(std::chrono::milliseconds(1000));
        handler.run_actions();
    }

    bool cleanup_ok = handler.unregister_event_handler(&timer);
    cleanup_ok = client.unregister_events(handler) && cleanup_ok;
    if (client.is_connected()) {
        cleanup_ok = !log_io_error(client.unsubscribe(pong_topic), "client_a unsubscribe") && cleanup_ok;
    }
    cleanup_ok = !log_io_error(client.disconnect(), "client_a disconnect") && cleanup_ok;
    if (!cleanup_ok) {
        return 1;
    }

    if (g_running != 0) {
        std::cerr << "ERROR: client_a stopped because SHM server disconnected\n";
        return 1;
    }

    std::cout << "SHM ping-pong client A stopped\n";
    return 0;
}
