// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 - 2026 Pionix GmbH and Contributors to EVerest
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include <everest/slac/timer.hpp>

using namespace everest::lib::slac;

namespace {

bool assert_true(bool cond, const char* test_name, const char* details) {
    if (not cond) {
        std::printf("[%s] FAIL: %s\n", test_name, details);
        return false;
    }
    return true;
}

bool test_set_duration_minutes_after_reset() {
    const char* test_name = "test_set_duration_minutes_after_reset";

    timer timer{};
    timer.setDuration(std::chrono::minutes(1));
    timer.reset();

    timer::clock::duration remaining_time{};
    auto timed_out = timer.getRemainingTime(remaining_time);
    auto remaining_seconds = std::chrono::duration_cast<std::chrono::seconds>(remaining_time);

    return assert_true(not timed_out, test_name, "timer reported timed out immediately after reset") &&
           assert_true(remaining_seconds.count() > 55 && remaining_seconds.count() <= 60, test_name,
                       "remaining time is not close to 60 seconds after setting 1 minute");
}

} // namespace

int main() {
    const auto tests = std::array<std::pair<const char*, bool (*)()>, 1>{
        std::make_pair("test_set_duration_minutes_after_reset", test_set_duration_minutes_after_reset),
    };

    int failed_count = 0;
    for (auto const& test : tests) {
        if (not test.second()) {
            std::printf("[FAIL] %s\n", test.first);
            ++failed_count;
        } else {
            std::printf("[PASS] %s\n", test.first);
        }
    }

    if (failed_count > 0) {
        std::printf("FAILED (%d)\n", failed_count);
        return EXIT_FAILURE;
    }

    std::printf("PASSED\n");
    return EXIT_SUCCESS;
}
