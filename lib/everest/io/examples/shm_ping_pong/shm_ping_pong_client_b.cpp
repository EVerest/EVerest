// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "shm_ping_pong_common.hpp"

#include <chrono>
#include <cstdint>
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

    shm::shm_client client(make_client_options(runtime, "client_b"));
    client.set_error_handler([](shm::io_status status, std::string_view message) {
        std::cerr << "ERROR: client_b: " << shm::to_string(status) << ": " << message << "\n";
    });

    std::cout << "SHM ping-pong client B\n";
    std::cout << "  control socket: " << runtime.control_socket << "\n";
    std::cout << "  subscribes: " << ping_topic << "\n";
    std::cout << "  publishes replies: " << pong_topic << "\n";
    std::cout << "  press Ctrl-C to stop\n" << std::flush;

    if (log_io_error(client.connect(), "client_b connect")) {
        return 1;
    }

    std::uint64_t pong_seq = 0;
    const auto subscribe_result =
        client.subscribe(ping_topic, [&client, &pong_seq](std::string_view topic, std::string_view payload) {
            std::cout << "client_b received " << topic << ": " << payload << "\n";

            const auto ping_seq = extract_json_uint(payload, "seq", 0);
            const auto response = make_pong_payload(++pong_seq, ping_seq);
            shm::publish_options options;
            options.full_buffer_behavior = shm::publish_full_buffer_behavior::fail;
            const auto result = client.publish(pong_topic, response, options);
            if (!result) {
                log_io_error(result, "client_b publish pong");
                return;
            }
            std::cout << "client_b published " << pong_topic << ": " << response << "\n";
        });
    if (log_io_error(subscribe_result, "client_b subscribe")) {
        (void)client.disconnect();
        return 1;
    }

    event::fd_event_handler handler;
    if (!client.register_events(handler)) {
        std::cerr << "ERROR: failed to register client_b SHM events\n";
        (void)client.disconnect();
        return 1;
    }

    while (g_running != 0 && client.is_connected()) {
        handler.poll(std::chrono::milliseconds(1000));
        handler.run_actions();
    }

    bool cleanup_ok = client.unregister_events(handler);
    if (client.is_connected()) {
        cleanup_ok = !log_io_error(client.unsubscribe(ping_topic), "client_b unsubscribe") && cleanup_ok;
    }
    cleanup_ok = !log_io_error(client.disconnect(), "client_b disconnect") && cleanup_ok;
    if (!cleanup_ok) {
        return 1;
    }

    if (g_running != 0) {
        std::cerr << "ERROR: client_b stopped because SHM server disconnected\n";
        return 1;
    }

    std::cout << "SHM ping-pong client B stopped\n";
    return 0;
}
