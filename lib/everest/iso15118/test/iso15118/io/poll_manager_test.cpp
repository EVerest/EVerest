// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <iterator>

#include <iso15118/io/poll_manager.hpp>

using namespace iso15118::io;

namespace {

std::size_t open_fd_count() {
    return static_cast<std::size_t>(
        std::distance(std::filesystem::directory_iterator("/proc/self/fd"), std::filesystem::directory_iterator{}));
}

} // namespace

SCENARIO("PollManager wakes up on abort() before any fd is registered") {
    GIVEN("A PollManager without registered fds") {
        PollManager poll_manager;

        WHEN("abort() is called and then poll() with a long timeout") {
            poll_manager.abort();
            const auto start = std::chrono::steady_clock::now();
            poll_manager.poll(5000);
            const auto elapsed = std::chrono::steady_clock::now() - start;

            THEN("poll() returns on the eventfd instead of waiting for the timeout") {
                REQUIRE(elapsed < std::chrono::milliseconds(1000));
            }
        }
    }
}

SCENARIO("PollManager releases its eventfd") {
    GIVEN("The number of open fds") {
        const auto before = open_fd_count();

        WHEN("PollManagers are created and destroyed") {
            for (int i = 0; i < 64; ++i) {
                const PollManager poll_manager;
            }

            THEN("no fd is left open") {
                REQUIRE(open_fd_count() == before);
            }
        }
    }
}
