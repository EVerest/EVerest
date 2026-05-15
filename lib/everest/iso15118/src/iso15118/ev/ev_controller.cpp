// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/ev_controller.hpp>

#include <optional>

#include <everest/io/event/timer_fd.hpp>
#include <everest/io/tcp/tcp_client.hpp>
#include <everest/io/udp/udp_client.hpp>

#include <iso15118/ev/io/sdp_client.hpp>

#include <iostream>

using namespace std::chrono_literals;

namespace iso15118::ev {

void EvController::start_session() {
    auto constexpr SDP_SERVER_PORT = 15118;
    auto constexpr SDP_SERVER_ADDRESS = "ff02::1";

    std::atomic_bool running{true};

    std::optional<io::SdpResponse> sdp_response{std::nullopt};

    everest::lib::io::udp::udp_client sdp_client(SDP_SERVER_ADDRESS, SDP_SERVER_PORT, 1000);
    sdp_client.set_error_handler(
        [](int error, const std::string& message) { std::cerr << "ERROR ( " << error << " ): " << message << "\n"; });
    sdp_client.set_rx_handler([&sdp_response, &running](const everest::lib::io::udp::udp_payload& payload,
                                                        everest::lib::io::udp::udp_client::interface& /*repl*/) {
        std::cout << "UDP payload: " << std::string(payload.buffer.begin(), payload.buffer.end()) << "\n";
        running = false; // TODO(SL): Check the norm if sdp should stop here or retrigger the sdp process

        if (payload.size() != io::SDP_RES_LENGTH) {
            // TODO(SL): Adding warning that udp_payload has not the correct size
            return;
        }

        std::array<uint8_t, io::SDP_RES_LENGTH> response{};
        std::copy(payload.buffer.begin(), payload.buffer.end(), response.begin());
        const auto sdp_response_ = io::SdpResponse(response);

        if (not sdp_response_) {
            // TODO(SL): Adding warning that V2GTP Header was not valid
            return;
        }

        sdp_response = std::make_optional(sdp_response_);
    });

    std::cout << "UDP socket ok? -> " << not sdp_client.on_error() << "\n";

    everest::lib::io::event::timer_fd sdp_timeout;
    sdp_timeout.set_timeout(500ms); // TODO(SL): Check that in the standard

    everest::lib::io::event::timer_fd comm_failure_timeout;
    comm_failure_timeout.set_timeout(18s); // TODO(SL): Check that in the standard

    auto sdp_request = io::SdpRequest(io::Security::NO_TRANSPORT_SECURITY, io::TransportProtocol::TCP);
    const auto sdp_req_buffer = sdp_request.create_req();

    ev_handler.register_event_handler(&sdp_client);
    ev_handler.register_event_handler(&sdp_timeout, [&sdp_client, &sdp_req_buffer](auto&) {
        everest::lib::io::udp::udp_payload payload;
        payload.set_message(sdp_req_buffer.data(), sdp_req_buffer.size());
        sdp_client.tx(payload);
    }); // count the sdp request and stop sending after a certain time
    ev_handler.register_event_handler(&comm_failure_timeout, [&running](auto&) { running = false; });

    // Send the first sdp req
    everest::lib::io::udp::udp_payload payload;
    payload.set_message(sdp_req_buffer.data(), sdp_req_buffer.size());
    sdp_client.tx(payload);

    ev_handler.run(running);
    ev_handler.unregister_event_handler(&sdp_client);
    ev_handler.unregister_event_handler(&sdp_timeout);
    ev_handler.unregister_event_handler(&comm_failure_timeout);

    if (not sdp_response.has_value()) {
        std::cout << "Stop session because there were no answer from a charger\n";
        return;
    }

    const auto& sdp_res_ref = sdp_response.value();
    const auto tcp_server_address = sdp_res_ref.address;
    const auto tcp_server_port = sdp_res_ref.port;

    // Create TCP Client
    everest::lib::io::tcp::tcp_client client(tcp_server_address, tcp_server_port, 1000);
    client.set_error_handler(
        [](int error, const std::string& message) { std::cerr << "ERROR ( " << error << " ): " << message << "\n"; });
    client.set_rx_handler(
        [](const everest::lib::io::event::fd_event_client<everest::lib::io::tcp::tcp_socket>::payload& payload,
           everest::lib::io::tcp::tcp_client::interface& /*repl*/) {
            std::cout << "TCP payload: " << std::string(payload.begin(), payload.end()) << "\n";
        });

    // 1. Send udp message
    // 2. Wait for udp res -> Adding timer to ev_handler
    // 3. After timeout send req message again (50x)
    // 4. Create TCP/TLS Client with SDP Res message
    // 5. Wait for complete handshake
    // 6. Send SAP Req and start state machine based on selected standard
    // 7. Statemachine ....
    // 8. Close session + TCP Client
    // 9. Return start_session() to signal that the hlc session stopped
    // TODO(SL): Start a loop here and return this function only after a session stopped vs
}

} // namespace iso15118::ev
