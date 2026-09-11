// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 - 2026 Pionix GmbH and Contributors to EVerest
//
// Carried over from slac_neo's telemetry test. This library publishes a Status rather than
// consuming one, so there is no deserialize() to round trip through; the assertions read the
// serialized JSON directly instead.
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

#include <everest/slac/status.hpp>
#include <nlohmann/json.hpp>

using namespace everest::slac;

namespace {

bool assert_true(bool cond, const char* test_name, const char* details) {
    if (not cond) {
        std::printf("[%s] FAIL: %s\n", test_name, details);
        return false;
    }
    return true;
}

bool test_default_status_serializes_with_zero_ev_mac() {
    const char* test_name = "test_default_status_serializes_with_zero_ev_mac";

    Status status;
    auto const serialized = nlohmann::json::parse(serialize(status));

    return assert_true(serialized.at("ev_mac") == "00:00:00:00:00:00", test_name,
                       "serialized ev_mac is not 00:00:00:00:00:00") &&
           assert_true(serialized.at("session_count") == 0, test_name, "default session_count is not zero") &&
           assert_true(serialized.at("modem_link_ready") == false, test_name, "default modem_link_ready is not false");
}

bool test_d3_state_spellings() {
    const char* test_name = "test_d3_state_spellings";

    return assert_true(to_string(D3State::Unmatched) == "UNMATCHED", test_name, "Unmatched spelled wrong") &&
           assert_true(to_string(D3State::Matching) == "MATCHING", test_name, "Matching spelled wrong") &&
           assert_true(to_string(D3State::Matched) == "MATCHED", test_name, "Matched spelled wrong");
}

} // namespace

int main() {
    const auto tests = std::array<std::pair<const char*, bool (*)()>, 2>{
        std::make_pair("test_default_status_serializes_with_zero_ev_mac",
                       test_default_status_serializes_with_zero_ev_mac),
        std::make_pair("test_d3_state_spellings", test_d3_state_spellings),
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
