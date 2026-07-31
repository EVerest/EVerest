// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

#include <iso15118/io/connection_client_plain.hpp>
#include <iso15118/io/connection_plain.hpp>
#include <iso15118/io/poll_manager.hpp>

using namespace iso15118;
using Event = io::ConnectionEvent;

namespace {
constexpr auto INTERFACE = "lo";
constexpr int MAX_POLL_ITERATIONS = 200;
constexpr int POLL_TIMEOUT_MS = 20;
} // namespace

SCENARIO("Plain TCP connection between ConnectionPlain server and ConnectionClientPlain") {

    GIVEN("A listening ConnectionPlain server on lo") {
        io::PollManager poll_manager;
        io::ConnectionPlain server(poll_manager, INTERFACE);

        std::vector<Event> server_events;
        std::vector<uint8_t> server_received;
        bool server_replied{false};

        server.set_event_callback([&](Event event) {
            server_events.push_back(event);
            if (event == Event::NEW_DATA) {
                std::array<uint8_t, 128> buffer{};
                const auto result = server.read(buffer.data(), buffer.size());
                server_received.insert(server_received.end(), buffer.begin(), buffer.begin() + result.bytes_read);
                if (not server_replied) {
                    const std::array<uint8_t, 5> reply{'w', 'o', 'r', 'l', 'd'};
                    server.write(reply.data(), reply.size());
                    server_replied = true;
                }
            }
        });

        const auto endpoint = server.get_public_endpoint();

        WHEN("A ConnectionClientPlain connects and both sides exchange data") {
            io::ConnectionClientPlain client(poll_manager, INTERFACE, endpoint);

            std::vector<Event> client_events;
            std::vector<uint8_t> client_received;

            client.set_event_callback([&](Event event) {
                client_events.push_back(event);
                if (event == Event::NEW_DATA) {
                    std::array<uint8_t, 128> buffer{};
                    const auto result = client.read(buffer.data(), buffer.size());
                    client_received.insert(client_received.end(), buffer.begin(), buffer.begin() + result.bytes_read);
                }
            });

            const std::array<uint8_t, 5> hello{'h', 'e', 'l', 'l', 'o'};
            client.write(hello.data(), hello.size());

            for (int i = 0; i < MAX_POLL_ITERATIONS; ++i) {
                if (server_received.size() >= hello.size() and client_received.size() >= 5) {
                    break;
                }
                poll_manager.poll(POLL_TIMEOUT_MS);
            }

            THEN("The events fire in order and the data is exchanged both ways") {
                REQUIRE(client_events.size() >= 3);
                REQUIRE(client_events[0] == Event::ACCEPTED);
                REQUIRE(client_events[1] == Event::OPEN);
                REQUIRE(client_events[2] == Event::NEW_DATA);

                REQUIRE(server_events.size() >= 3);
                REQUIRE(server_events[0] == Event::ACCEPTED);
                REQUIRE(server_events[1] == Event::OPEN);
                REQUIRE(server_events[2] == Event::NEW_DATA);

                const std::vector<uint8_t> expected_server_rx{'h', 'e', 'l', 'l', 'o'};
                const std::vector<uint8_t> expected_client_rx{'w', 'o', 'r', 'l', 'd'};
                REQUIRE(server_received == expected_server_rx);
                REQUIRE(client_received == expected_client_rx);
            }
        }
    }
}
