// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// Started by clockshift_test with the shim preloaded: prints the steady clock and CLOCK_MONOTONIC_RAW
// in nanoseconds, waits for a line on stdin, then prints both again.

#include <chrono>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <string>

namespace {
std::int64_t steady_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::int64_t monotonic_raw_ns() {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC_RAW, &now);
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::seconds(now.tv_sec) +
                                                                std::chrono::nanoseconds(now.tv_nsec))
        .count();
}

void print_clocks() {
    // Flushed explicitly: the test reads this through a pipe while the probe keeps running.
    std::cout << steady_ns() << " " << monotonic_raw_ns() << '\n' << std::flush;
}
} // namespace

int main() {
    print_clocks();
    std::string line;
    std::getline(std::cin, line);
    print_clocks();
    return 0;
}
