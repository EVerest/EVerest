// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 - 2026 Pionix GmbH and Contributors to EVerest

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include <everest/slac/timer.hpp>

using namespace everest::lib::slac;
using namespace std::chrono_literals;

namespace {

bool assert_true(bool cond, const char* test_name, const char* details) {
    if (not cond) {
        std::printf("[%s] FAIL: %s\n", test_name, details);
        return false;
    }
    return true;
}

const timer::tp t0 = timer::tp{} + 1s;

bool test_default_constructed_timer_is_expired() {
    const char* test_name = "test_default_constructed_timer_is_expired";
    timer t{};
    return assert_true(t.expired(t0), test_name, "a never armed timer must read as expired");
}

bool test_armed_timer_expires_strictly_after_deadline() {
    const char* test_name = "test_armed_timer_expires_strictly_after_deadline";
    timer t{};
    t.arm(t0, 100ms);
    return assert_true(not t.expired(t0), test_name, "must not be expired when armed") and
           assert_true(not t.expired(t0 + 99ms), test_name, "must not be expired before the deadline") and
           assert_true(not t.expired(t0 + 100ms), test_name, "must not be expired in the deadline tick itself") and
           assert_true(t.expired(t0 + 101ms), test_name, "must be expired after the deadline") and
           assert_true(t.deadline() == t0 + 100ms, test_name, "deadline must be reference + duration") and
           assert_true(t.remaining(t0 + 30ms) == 70ms, test_name, "remaining must count down to the deadline");
}

bool test_reset_restarts_countdown_with_same_duration() {
    const char* test_name = "test_reset_restarts_countdown_with_same_duration";
    timer t{};
    t.arm(t0, 100ms);
    t.reset(t0 + 60ms);
    return assert_true(not t.expired(t0 + 150ms), test_name, "reset must move the deadline") and
           assert_true(t.expired(t0 + 161ms), test_name, "reset must keep the duration");
}

bool test_set_duration_keeps_reference() {
    const char* test_name = "test_set_duration_keeps_reference";
    timer t{};
    t.arm(t0, 100ms);
    t.set_duration(50ms);
    return assert_true(t.deadline() == t0 + 50ms, test_name, "set_duration must not move the reference") and
           assert_true(t.expired(t0 + 51ms), test_name, "the shorter duration must apply immediately");
}

bool test_set_duration_ms_matches_chrono() {
    const char* test_name = "test_set_duration_ms_matches_chrono";
    timer a{};
    timer b{};
    a.arm(t0, 250ms);
    b.set_duration_ms(250);
    b.reset(t0);
    return assert_true(a.deadline() == b.deadline(), test_name, "set_duration_ms must equal set_duration(ms)");
}

} // namespace

int main() {
    const auto tests = std::array<std::pair<const char*, bool (*)()>, 5>{
        std::make_pair("test_default_constructed_timer_is_expired", test_default_constructed_timer_is_expired),
        std::make_pair("test_armed_timer_expires_strictly_after_deadline",
                       test_armed_timer_expires_strictly_after_deadline),
        std::make_pair("test_reset_restarts_countdown_with_same_duration",
                       test_reset_restarts_countdown_with_same_duration),
        std::make_pair("test_set_duration_keeps_reference", test_set_duration_keeps_reference),
        std::make_pair("test_set_duration_ms_matches_chrono", test_set_duration_ms_matches_chrono),
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
